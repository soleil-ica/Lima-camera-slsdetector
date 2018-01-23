//###########################################################################
// This file is part of LImA, a Library for Image Acquisition
//
// Copyright (C) : 2009-2011
// European Synchrotron Radiation Facility
// BP 220, Grenoble 38043
// FRANCE
//
// This is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This software is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//###########################################################################

#include "SlsDetectorCamera.h"

#include "lima/Timestamp.h"
#include "lima/CtAcquisition.h"
#include "lima/CtSaving.h"
#include "lima/SoftOpExternalMgr.h"
#include "lima/RegExUtils.h"

#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

using namespace std;
using namespace lima;
using namespace lima::SlsDetector;

bool CPUAffinity::UseSudo = true;

int CPUAffinity::findNbCPUs()
{
	DEB_STATIC_FUNCT();
	int nb_cpus = 0;
	const char *proc_file_name = "/proc/cpuinfo";
	ifstream proc_file(proc_file_name);
	while (proc_file) {
		char buffer[1024];
		proc_file.getline(buffer, sizeof(buffer));
		istringstream is(buffer);
		string t;
		is >> t;
		if (t == "processor")
			++nb_cpus;
	}
	DEB_RETURN() << DEB_VAR1(nb_cpus);
	return nb_cpus;
}

int CPUAffinity::findMaxNbCPUs()
{
	DEB_STATIC_FUNCT();
	NumericGlob proc_glob("/proc/sys/kernel/sched_domain/cpu");
	int max_nb_cpus = proc_glob.getNbEntries();
	DEB_RETURN() << DEB_VAR1(max_nb_cpus);
	return max_nb_cpus;
}

int CPUAffinity::getNbCPUs(bool max_nb)
{
	static int nb_cpus = 0;
	EXEC_ONCE(nb_cpus = findNbCPUs());
	static int max_nb_cpus = 0;
	EXEC_ONCE(max_nb_cpus = findMaxNbCPUs());
	int cpus = (max_nb && max_nb_cpus) ? max_nb_cpus : nb_cpus;
	return cpus;
}

std::string CPUAffinity::getProcDir(bool local_threads)
{
	DEB_STATIC_FUNCT();
	DEB_PARAM() << DEB_VAR1(local_threads);
	ostringstream os;
	os << "/proc/";
	if (local_threads)
		os << getpid() << "/task/";
	string proc_dir = os.str();
	DEB_RETURN() << DEB_VAR1(proc_dir);
	return proc_dir;
}

std::string CPUAffinity::getTaskProcDir(pid_t task, bool is_thread)
{
	DEB_STATIC_FUNCT();
	DEB_PARAM() << DEB_VAR2(task, is_thread);
	ostringstream os;
	os << getProcDir(is_thread) << task << "/";
	string proc_dir = os.str();
	DEB_RETURN() << DEB_VAR1(proc_dir);
	return proc_dir;
}

void CPUAffinity::initCPUSet(cpu_set_t& cpu_set) const
{
	CPU_ZERO(&cpu_set);
	uint64_t mask = *this;
	for (unsigned int i = 0; i < sizeof(mask) * 8; ++i) {
		if ((mask >> i) & 1)
			CPU_SET(i, &cpu_set);
	}
}

void CPUAffinity::applyToTask(pid_t task, bool incl_threads,
				      bool use_taskset) const
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR3(task, incl_threads, use_taskset);

	string proc_status = getTaskProcDir(task, !incl_threads) + "status";
	if (access(proc_status.c_str(), F_OK) != 0)
		return;

	if (use_taskset)
		applyWithTaskset(task, incl_threads);
	else
		applyWithSetAffinity(task, incl_threads);
}

void CPUAffinity::applyWithTaskset(pid_t task, bool incl_threads) const
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR2(task, incl_threads);

	ostringstream os;
	uint64_t mask = *this;
	if (UseSudo)
		os << "sudo ";
	const char *all_tasks_opt = incl_threads ? "-a " : "";
	os << "taskset " << all_tasks_opt
	   << "-p " << hex << showbase << mask << " " 
	   << dec << noshowbase << task;
	if (!DEB_CHECK_ANY(DebTypeTrace))
		os << " > /dev/null 2>&1";
	DEB_TRACE() << "executing: '" << os.str() << "'";
	int ret = system(os.str().c_str());
	if (ret != 0) {
		const char *th = incl_threads ? "and threads " : "";
		THROW_HW_ERROR(Error) << "Error setting task " << task 
				      << " " << th << "CPU affinity";
	}
}

void CPUAffinity::applyWithSetAffinity(pid_t task, bool incl_threads)
									const
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR2(task, incl_threads);

	IntList task_list;
	if (incl_threads) {
		string proc_dir = getTaskProcDir(task, false) + "task/";
		NumericGlob proc_glob(proc_dir);
		typedef NumericGlob::IntStringList IntStringList;
		IntStringList list = proc_glob.getIntPathList();
		if (list.empty())
			THROW_HW_ERROR(Error) << "Cannot find task " << task 
					      << " threads";
		IntStringList::const_iterator it, end = list.end();
		for (it = list.begin(); it != end; ++it)
			task_list.push_back(it->first);
	} else {
		task_list.push_back(task);
	}

	cpu_set_t cpu_set;
	initCPUSet(cpu_set);
	IntList::const_iterator it, end = task_list.end();
	for (it = task_list.begin(); it != end; ++it) {
		DEB_TRACE() << "setting " << task << " CPU mask: " << *this;
		int ret = sched_setaffinity(task, sizeof(cpu_set), &cpu_set);
		if (ret != 0) {
			const char *th = incl_threads ? "and threads " : "";
			THROW_HW_ERROR(Error) << "Error setting task " << task 
					      << " " << th << "CPU affinity";
		}
	}
}

ProcCPUAffinityMgr::WatchDog::WatchDog()
{
	DEB_CONSTRUCTOR();

	m_lima_pid = getpid();

	m_child_pid = fork();
	if (m_child_pid == 0) {
		m_child_pid = getpid();
		DEB_TRACE() << DEB_VAR2(m_lima_pid, m_child_pid);

		m_cmd_pipe.close(Pipe::WriteFd);
		m_res_pipe.close(Pipe::ReadFd);

		signal(SIGINT, SIG_IGN);
		signal(SIGTERM, sigTermHandler);

		childFunction();
		_exit(0);
	} else {
		m_cmd_pipe.close(Pipe::ReadFd);
		m_res_pipe.close(Pipe::WriteFd);

		sendChildCmd(Init);
		DEB_TRACE() << "Child is ready";
	}
}

ProcCPUAffinityMgr::WatchDog::~WatchDog()
{
	DEB_DESTRUCTOR();

	if (!childEnded()) {
		sendChildCmd(CleanUp);
		waitpid(m_child_pid, NULL, 0);
	}
}

void ProcCPUAffinityMgr::WatchDog::sigTermHandler(int /*signo*/)
{
}

bool ProcCPUAffinityMgr::WatchDog::childEnded()
{
	return (waitpid(m_child_pid, NULL, WNOHANG) != 0);
}

void ProcCPUAffinityMgr::WatchDog::sendChildCmd(Cmd cmd, Arg arg)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR2(cmd, DebHex(arg));

	if (childEnded())
		THROW_HW_ERROR(Error) << "Watchdog child process killed: " 
				      << m_child_pid;
	Packet packet = {cmd, arg};
	void *p = static_cast<void *>(&packet);
	string s(static_cast<char *>(p), sizeof(packet));
	m_cmd_pipe.write(s);

	s = m_res_pipe.read(1);
	Cmd res = Cmd(s.data()[0]);
	if (res != Ok)
		THROW_HW_ERROR(Error) << "Invalid watchdog child ack";
	DEB_TRACE() << "Watchdog child acknowledged Ok";
}

ProcCPUAffinityMgr::WatchDog::Packet
ProcCPUAffinityMgr::WatchDog::readParentCmd()
{
	DEB_MEMBER_FUNCT();
	Packet packet;
	string s = m_cmd_pipe.read(sizeof(packet));
	if (s.empty())
		THROW_HW_ERROR(Error) << "Watchdog cmd pipe closed/intr";
	memcpy(&packet, s.data(), s.size());
	DEB_RETURN() << DEB_VAR2(packet.cmd, DebHex(packet.arg));
	return packet;
}

void ProcCPUAffinityMgr::WatchDog::ackParentCmd()
{
	DEB_MEMBER_FUNCT();
	m_res_pipe.write(string(1, Ok));
}

void ProcCPUAffinityMgr::WatchDog::childFunction()
{
	DEB_MEMBER_FUNCT();

	Packet first = readParentCmd();
	if (first.cmd != Init) {
		DEB_ERROR() << "Invalid watchdog init cmd: " << first.cmd;
		return;
	}
	ackParentCmd();

	bool last_was_clean = false;
	bool cleanup_req = false;

	try {
		do {
			Packet packet = readParentCmd();
			if (packet.cmd == SetAffinity) {
				CPUAffinity cpu_affinity = packet.arg;
				affinitySetter(cpu_affinity);
				ackParentCmd();
				last_was_clean = cpu_affinity.isDefault();
			} else if (packet.cmd == CleanUp) {
				cleanup_req = true;
			}
		} while (!cleanup_req);

	} catch (...) {
		DEB_ALWAYS() << "Watchdog parent and/or child killed!";
	}

	DEB_TRACE() << "Clean-up";
	if (!last_was_clean)
		affinitySetter(CPUAffinity());

	if (cleanup_req)
		ackParentCmd(); 
}

void 
ProcCPUAffinityMgr::WatchDog::affinitySetter(CPUAffinity cpu_affinity)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR1(cpu_affinity);

	ProcList proc_list = getOtherProcList(cpu_affinity);
	if (proc_list.empty())
		return;

	DEB_ALWAYS() << "Setting CPUAffinity for " << PrettyIntList(proc_list)
		     << " to " << cpu_affinity;
	ProcList::const_iterator it, end = proc_list.end();
	for (it = proc_list.begin(); it != end; ++it)
		cpu_affinity.applyToTask(*it);
	DEB_ALWAYS() << "Done!";
}

ProcList 
ProcCPUAffinityMgr::WatchDog::getOtherProcList(CPUAffinity cpu_affinity)
{
	DEB_MEMBER_FUNCT();

	ProcList proc_list = getProcList(NoMatchAffinity, cpu_affinity);
	pid_t ignore_proc_list[] = { m_lima_pid, m_child_pid, 0 };
	for (pid_t *p = ignore_proc_list; *p; ++p) {
		ProcList::iterator it, end = proc_list.end();
		it = find(proc_list.begin(), end, *p);
		if (it != end)
			proc_list.erase(it);
	}

	return proc_list;
}

void 
ProcCPUAffinityMgr::WatchDog::setOtherCPUAffinity(CPUAffinity cpu_affinity)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR1(cpu_affinity);
	sendChildCmd(SetAffinity, cpu_affinity);
}

ProcCPUAffinityMgr::ProcCPUAffinityMgr()
{
	DEB_CONSTRUCTOR();
}

ProcCPUAffinityMgr::~ProcCPUAffinityMgr()
{
	DEB_DESTRUCTOR();
}

ProcList
ProcCPUAffinityMgr::getProcList(Filter filter, CPUAffinity cpu_affinity)
{
	DEB_STATIC_FUNCT();
	DEB_PARAM() << DEB_VAR2(filter, cpu_affinity);

	ProcList proc_list;
	bool this_proc = filter & ThisProc;
	filter = Filter(filter & ~ThisProc);
	string proc_dir = CPUAffinity::getProcDir(this_proc);
	NumericGlob proc_glob(proc_dir, "/status");
	NumericGlob::IntStringList list = proc_glob.getIntPathList();
	NumericGlob::IntStringList::const_iterator it, end = list.end();
	for (it = list.begin(); it != end; ++it) {
		int pid = it->first;
		const string& fname = it->second;
		bool has_vm = false;
		bool has_good_affinity = (filter == All);
		ifstream status_file(fname.c_str());
		while (status_file) {
			string s;
			status_file >> s;
			RegEx re;
			FullMatch full_match;

			re = "VmSize:?$";
			if (re.match(s, full_match)) {
				has_vm = true;
				continue;
			}

			re = "Cpus_allowed:?$";
			if (re.match(s, full_match) && (filter != All)) {
				uint64_t int_affinity;
				status_file >> hex >> int_affinity >> dec;
				CPUAffinity affinity = int_affinity;
				bool aff_match = (affinity == cpu_affinity);
				bool filt_match = (filter == MatchAffinity);
				has_good_affinity = (aff_match == filt_match);
				continue;
			}
		}
		DEB_TRACE() << DEB_VAR3(pid, has_vm, has_good_affinity);
		if (has_vm && has_good_affinity)
			proc_list.push_back(pid);
	}

	if (DEB_CHECK_ANY(DebTypeReturn))
		DEB_RETURN() << DEB_VAR1(PrettyList<ProcList>(proc_list));
	return proc_list;
}


ProcList
ProcCPUAffinityMgr::getThreadList(Filter filter, 
					  CPUAffinity cpu_affinity)
{
	DEB_STATIC_FUNCT();
	DEB_PARAM() << DEB_VAR2(filter, cpu_affinity);
	return getProcList(Filter(filter | ThisProc), cpu_affinity);
}

void ProcCPUAffinityMgr::setOtherCPUAffinity(CPUAffinity cpu_affinity)
{
	DEB_MEMBER_FUNCT();

	if (!m_watchdog || m_watchdog->childEnded())
		m_watchdog = new WatchDog();

	m_watchdog->setOtherCPUAffinity(cpu_affinity);

	if (cpu_affinity.isDefault())
		m_watchdog = NULL;
}

SystemCPUAffinityMgr::
ProcessingFinishedEvent::ProcessingFinishedEvent(SystemCPUAffinityMgr *mgr)
	: m_mgr(mgr), m_cb(this), m_ct(NULL)
{
	DEB_CONSTRUCTOR();

	m_nb_frames = 0;
	m_cnt_act = false;
	m_saving_act = false;
	m_stopped = false;
	m_last_cb_ts = Timestamp::now();
}

SystemCPUAffinityMgr::
ProcessingFinishedEvent::~ProcessingFinishedEvent()
{
	DEB_DESTRUCTOR();
	if (m_mgr)
		m_mgr->m_proc_finished = NULL;
}

void SystemCPUAffinityMgr::ProcessingFinishedEvent::prepareAcq()
{
	DEB_MEMBER_FUNCT();
	if (!m_ct)
		return;

	CtAcquisition *acq = m_ct->acquisition();
	acq->getAcqNbFrames(m_nb_frames);

	SoftOpExternalMgr *extOp = m_ct->externalOperation();
	if (DEB_CHECK_ANY(DebTypeTrace)) {
		typedef list<string> NameList;
		typedef map<int, NameList> OpStageNameListMap;
		OpStageNameListMap active_op;
		extOp->getActiveOp(active_op);
		OpStageNameListMap::const_iterator mit, mend = active_op.end();
		ostringstream os;
		os << "{";
		for (mit = active_op.begin(); mit != mend; ++mend) {
			const int& stage = mit->first;
			os << stage << ": [";
			const NameList& name_list = mit->second;
			NameList::const_iterator lit, lend = name_list.end();
			const char *sep = "";
			for (lit = name_list.begin(); lit != lend; ++lit) {
				const string& name = *lit;
				os << sep << name;
				sep = ",";
			}
			os << "]";
		}
		os << "}";
		DEB_TRACE() << "extOp->getActiveOp()=" << os.str();
	}

	bool extOp_link_task_act, extOp_sink_task_act;
	extOp->isTaskActive(extOp_link_task_act, extOp_sink_task_act);
	DEB_TRACE() << DEB_VAR2(extOp_link_task_act, extOp_sink_task_act);
	m_cnt_act = extOp_sink_task_act;

	CtSaving *saving = m_ct->saving();
	CtSaving::SavingMode mode;
	saving->getSavingMode(mode);
	m_saving_act = (mode != CtSaving::Manual);

	DEB_TRACE() << DEB_VAR3(m_nb_frames, m_saving_act, m_cnt_act);

	m_stopped = false;
}

void SystemCPUAffinityMgr::ProcessingFinishedEvent::stopAcq()
{
	DEB_MEMBER_FUNCT();
	m_stopped = true;
}

void SystemCPUAffinityMgr::ProcessingFinishedEvent::processingFinished()
{
	DEB_MEMBER_FUNCT();
	if (m_mgr)
		m_mgr->limaFinished();
}

void SystemCPUAffinityMgr::
ProcessingFinishedEvent::registerStatusCallback(CtControl *ct)
{
	DEB_MEMBER_FUNCT();
	if (m_ct)
		THROW_HW_ERROR(Error) << "StatusCallback already registered";

	ct->registerImageStatusCallback(m_cb);
	m_ct = ct;
}

void SystemCPUAffinityMgr::
ProcessingFinishedEvent::limitUpdateRate()
{
	Timestamp next_ts = m_last_cb_ts + Timestamp(1.0 / 10);
	double remaining = next_ts - Timestamp::now();
	if (remaining > 0)
		Sleep(remaining);
}

void SystemCPUAffinityMgr::
ProcessingFinishedEvent::updateLastCallbackTimestamp()
{
	m_last_cb_ts = Timestamp::now();
}

Timestamp SystemCPUAffinityMgr::
ProcessingFinishedEvent::getLastCallbackTimestamp()
{
	return m_last_cb_ts;
}

void SystemCPUAffinityMgr::
ProcessingFinishedEvent::imageStatusChanged(
					const CtControl::ImageStatus& status)
{
	DEB_MEMBER_FUNCT();

	limitUpdateRate();
	updateLastCallbackTimestamp();

	int max_frame = m_nb_frames - 1; 
	int last_frame = status.LastImageAcquired;
	bool finished = (m_stopped || (last_frame == max_frame));
	finished &= (status.LastImageReady == last_frame);
	finished &= (!m_cnt_act || (status.LastCounterReady == last_frame));
	finished &= (m_stopped || !m_saving_act || 
		     (status.LastImageSaved == last_frame));
	if (finished)
		processingFinished();
}

SystemCPUAffinityMgr::SystemCPUAffinityMgr(Camera *cam)
	: m_cam(cam), m_proc_finished(NULL), 
	  m_lima_finished_timeout(3)
{
	DEB_CONSTRUCTOR();

	m_state = Ready;
}

SystemCPUAffinityMgr::~SystemCPUAffinityMgr()
{
	DEB_DESTRUCTOR();

	setLimaAffinity(CPUAffinity());

	if (m_proc_finished)
		m_proc_finished->m_mgr = NULL;
}

void SystemCPUAffinityMgr::applyAndSet(const SystemCPUAffinity& o)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR1(o);

	if (!m_cam)
		THROW_HW_ERROR(InvalidValue) << "apply without camera";

	setLimaAffinity(o.lima);
	setRecvAffinity(o.recv);

	if (!m_proc_mgr)
		m_proc_mgr = new ProcCPUAffinityMgr();

	m_proc_mgr->setOtherCPUAffinity(o.other);
	m_curr.other = o.other;

	m_set = o;
}

void SystemCPUAffinityMgr::setLimaAffinity(CPUAffinity lima_affinity)
{
	DEB_MEMBER_FUNCT();

	if (lima_affinity == m_curr.lima)
		return;

	if (m_lima_tids.size()) {
		ProcList::const_iterator it, end = m_lima_tids.end();
		for (it = m_lima_tids.begin(); it != end; ++it)
			// do not use taskset!
			lima_affinity.applyToTask(*it, false, false);
	} else {
		pid_t pid = getpid();
		lima_affinity.applyToTask(pid, true);
		m_curr.recv = lima_affinity;
	}
	m_curr.lima = lima_affinity;
}

void SystemCPUAffinityMgr::setRecvAffinity(CPUAffinity recv_affinity)
{
	DEB_MEMBER_FUNCT();

	if (recv_affinity == m_curr.recv)
		return;

	m_cam->setRecvCPUAffinity(recv_affinity);
	m_curr.recv = recv_affinity;
}

void SystemCPUAffinityMgr::updateRecvRestart()
{
	DEB_MEMBER_FUNCT();
	m_curr.recv = m_curr.lima;
}

SystemCPUAffinityMgr::ProcessingFinishedEvent *
SystemCPUAffinityMgr::getProcessingFinishedEvent()
{
	DEB_MEMBER_FUNCT();
	if (!m_proc_finished)
		m_proc_finished = new ProcessingFinishedEvent(this);
	return m_proc_finished;
}

void SystemCPUAffinityMgr::prepareAcq()
{
	DEB_MEMBER_FUNCT();
	AutoMutex l = lock();
	if (m_state != Ready)
		THROW_HW_ERROR(Error) << "SystemCPUAffinityMgr is not Ready: "
				      << "missing ProcessingFinishedEvent";
	if (m_proc_finished)
		m_proc_finished->prepareAcq();
	m_lima_tids.clear();
}

void SystemCPUAffinityMgr::startAcq()
{
	DEB_MEMBER_FUNCT();
	AutoMutex l = lock();
	m_state = Acquiring;
}

void SystemCPUAffinityMgr::stopAcq()
{
	DEB_MEMBER_FUNCT();
	if (m_proc_finished)
		m_proc_finished->stopAcq();
}

void SystemCPUAffinityMgr::recvFinished()
{
	DEB_MEMBER_FUNCT();

	AutoMutex l = lock();
	if (!m_proc_finished)
		m_state = Ready;
	if (m_state == Ready) 
		return;

	if (m_curr.lima != m_curr.recv) {
		m_state = Changing;
		AutoMutexUnlock u(l);
		ProcCPUAffinityMgr::Filter filter;
		filter = ProcCPUAffinityMgr::MatchAffinity;
		m_lima_tids = ProcCPUAffinityMgr::getThreadList(filter,
								m_curr.lima);
		DEB_ALWAYS() << "Lima TIDs: " << PrettyIntList(m_lima_tids);
		CPUAffinity lima_affinity = (uint64_t(m_curr.lima) | 
					     uint64_t(m_curr.recv));
		DEB_ALWAYS() << "Allowing Lima to run on Recv CPUs: " 
			     << lima_affinity;
		setLimaAffinity(lima_affinity);
	}

	m_state = Processing;
	m_cond.broadcast();
}

void SystemCPUAffinityMgr::limaFinished()
{
	DEB_MEMBER_FUNCT();

	AutoMutex l = lock();
	if (m_state == Acquiring)
		m_state = Ready;
	while ((m_state != Processing) && (m_state != Ready))
		m_cond.wait();
	if (m_state == Ready)
		return;

	if (m_curr.lima != m_set.lima) {
		m_state = Restoring;
		AutoMutexUnlock u(l);
		DEB_ALWAYS() << "Restoring Lima to dedicated CPUs: " 
			     << m_set.lima;
		setLimaAffinity(m_set.lima);
		setRecvAffinity(m_set.recv);
	}

	m_state = Ready;
	m_cond.broadcast();
}

void SystemCPUAffinityMgr::waitLimaFinished()
{
	DEB_MEMBER_FUNCT();

	if (!m_proc_finished)
		return;

	m_proc_finished->updateLastCallbackTimestamp();

	AutoMutex l = lock();
	while (m_state != Ready) {
		if (m_cond.wait(1))
			continue;

		AutoMutexUnlock u(l);
		Timestamp ts = m_proc_finished->getLastCallbackTimestamp();
		double elapsed = Timestamp::now() - ts;
		if (elapsed < m_lima_finished_timeout)
			continue;

		DEB_ERROR() << "No ImageStatusCallback in " << elapsed << " s";
		limaFinished();
	}
}

ostream& lima::SlsDetector::operator <<(ostream& os, const CPUAffinity& a)
{
	return os << hex << showbase << uint64_t(a) << dec << noshowbase;
}

ostream& lima::SlsDetector::operator <<(ostream& os, const SystemCPUAffinity& a)
{
	os << "<";
	os << "recv=" << a.recv << ", lima=" << a.lima << ", other=" << a.other;
	return os << ">";
}

ostream& 
lima::SlsDetector::operator <<(ostream& os, const PixelDepthCPUAffinityMap& m)
{
	os << "[";
	bool first = true;
	PixelDepthCPUAffinityMap::const_iterator it, end = m.end();
	for (it = m.begin(); it != end; ++it, first=false)
		os << (!first ? ", " : "") << it->first << ": " << it->second;
	return os << "]";
}
