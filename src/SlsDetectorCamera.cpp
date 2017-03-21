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

using namespace std;
using namespace lima;
using namespace lima::SlsDetector;


Args::Args()
{
	DEB_CONSTRUCTOR();
	update_argc_argv();
}

Args::Args(unsigned int argc, char *argv[])
{
	DEB_CONSTRUCTOR();
	for (unsigned int i = 0; i < argc; ++i)
		m_arg_list.push_back(argv[i]);
	update_argc_argv();
}

Args::Args(const string& s)
{
	DEB_CONSTRUCTOR();
	set(s);
}

Args::Args(const Args& o) : m_arg_list(o.m_arg_list)
{
	DEB_CONSTRUCTOR();
	update_argc_argv();
}

void Args::set(const string& s)
{
	DEB_MEMBER_FUNCT();
	m_arg_list.clear();
	istringstream is(s);
	while (is) {
		string token;
		is >> token;
		m_arg_list.push_back(token);
	}
	update_argc_argv();
}

void Args::clear()
{
	DEB_MEMBER_FUNCT();
	m_arg_list.clear();
	update_argc_argv();
}

Args& Args::operator =(const std::string& s)
{
	DEB_MEMBER_FUNCT();
	set(s);
	return *this;
}

string Args::pop_front()
{
	DEB_MEMBER_FUNCT();
	string s = m_arg_list[0];
	erase(0);
	return s;
}

void Args::erase(int pos)
{
	DEB_MEMBER_FUNCT();
	m_arg_list.erase(m_arg_list.begin() + pos);
	update_argc_argv();
}

void Args::update_argc_argv()
{
	DEB_MEMBER_FUNCT();
	m_argc = m_arg_list.size();
	m_argv = new char *[m_argc + 1];
	for (unsigned int i = 0; i < m_argc; ++i)
		m_argv[i] = const_cast<char *>(m_arg_list[i].c_str());
	m_argv[m_argc] = NULL;
}

ostream& lima::SlsDetector::operator <<(ostream& os, State state)
{
	const char *name = "Unknown";
	switch (state) {
	case Idle:		name = "Idle";		break;
	case Init:		name = "Init";		break;
	case Starting:		name = "Starting";	break;
	case Running:		name = "Running";	break;
	case StopReq:		name = "StopReq";	break;
	case Stopping:		name = "Stopping";	break;
	case Stopped:		name = "Stopped";	break;
	}
	return os << name;
}

Camera::Model::Model(Camera *cam, Type type)
	: m_cam(cam), m_type(type)
{
	DEB_CONSTRUCTOR();
	DEB_PARAM() << DEB_VAR1(type);

	m_cam->setModel(this);
}

Camera::Model::~Model()
{
	DEB_DESTRUCTOR();

	if (m_cam)
		m_cam->setModel(NULL);
}

void Camera::Model::putCmd(const string& s, int idx)
{
	DEB_MEMBER_FUNCT();
	m_cam->putCmd(s, idx);
}

string Camera::Model::getCmd(const string& s, int idx)
{
	DEB_MEMBER_FUNCT();
	return m_cam->getCmd(s, idx);
}

Camera::AppInputData::AppInputData(string cfg_fname) 
	: config_file_name(cfg_fname)
{
	DEB_CONSTRUCTOR();
	parseConfigFile();
}

void Camera::AppInputData::parseConfigFile()
{
	DEB_MEMBER_FUNCT();

	ifstream config_file(config_file_name.c_str());
	while (config_file) {
		string s;
		config_file >> s;
		RegEx re;
		FullMatch full_match;
		MatchList match_list;
		MatchListIt lit, lend;

		re = "hostname";
		if (re.match(s, full_match)) {
			string host_name;
			config_file >> host_name;
			RegEx re("([A-Za-z0-9]+)\\+?");
			re.multiSearch(host_name, match_list);
			lend = match_list.end();
			for (lit = match_list.begin(); lit != lend; ++lit) {
				const FullMatch& full_match = *lit;
				const SingleMatch& single_match = full_match[1];
				host_name_list.push_back(single_match);
			}
			continue;
		}

		re = "([0-9]+):rx_tcpport";
		if (re.match(s, full_match)) {
			istringstream is(full_match[1]);
			int id;
			is >> id;
			if (id < 0)
				THROW_HW_FATAL(InvalidValue) << 
					"Invalid detector id: " << id;
			int rx_tcpport;
			config_file >> rx_tcpport;
			recv_port_map[id] = rx_tcpport;
			continue;
		}
	}
}


Camera::FrameMap::Callback::Callback()
	: m_map(NULL)
{
	DEB_CONSTRUCTOR();
}

Camera::FrameMap::Callback::~Callback()
{
	DEB_DESTRUCTOR();
	if (m_map)
		m_map->m_cb = NULL;
}

Camera::FrameMap::FrameMap(bool debug)
	: m_nb_items(0), m_last_seq_finished_frame(-1), m_cb(NULL),
	  m_debug(debug)
{
	DEB_CONSTRUCTOR();
}

Camera::FrameMap::~FrameMap()
{
	DEB_DESTRUCTOR();
	if (m_cb)
		m_cb->m_map = NULL;
}

void Camera::FrameMap::setNbItems(int nb_items)
{
	DEB_MEMBER_FUNCT();
	m_nb_items = nb_items;
}

void Camera::FrameMap::clear()
{
	DEB_MEMBER_FUNCT();
	m_map.clear();
	m_non_seq_finished_frames.clear();
	m_last_seq_finished_frame = -1;
}

void Camera::FrameMap::setCallback(Callback *cb)
{ 
	DEB_MEMBER_FUNCT();
	if (m_cb)
		m_cb->m_map = NULL;
	m_cb = cb; 
	if (m_cb)
		m_cb->m_map = this;
}

void Camera::FrameMap::frameItemFinished(int frame, int item)
{
	DEB_MEMBER_FUNCT();

	if (m_nb_items == 0)		
		THROW_HW_ERROR(InvalidValue) << "No items defined";
	else if ((item < 0) || (item >= m_nb_items))
		THROW_HW_ERROR(InvalidValue) << DEB_VAR2(item, m_nb_items);
	Map::iterator mit = m_map.find(frame);
	if (mit == m_map.end()) {
		if (m_debug) {
			for (int i = 0; i < m_nb_items; ++i)
				if (i != item)
					m_map[frame].insert(i);
		} else {
			m_map[frame].insert(1);
		}
		return;
	}

	List& item_list = mit->second;
	if (m_debug) {
		List::iterator lit = item_list.find(item);
		if (lit == item_list.end())
			THROW_HW_ERROR(Error) << "item " << item << " already "
					      << "finished for frame " << frame;
		item_list.erase(lit);
		if (!item_list.empty())
			return;
	} else {
		List::iterator lit = item_list.begin();
		int nb_items = *lit + 1;
		item_list.erase(lit);
		item_list.insert(nb_items);
		if (nb_items != m_nb_items)
			return;
	}
	m_map.erase(mit);

	int &last = m_last_seq_finished_frame;
	List &waiting = m_non_seq_finished_frames;
	List::iterator lit;
	if (frame == last + 1) {
		++last;
		while ((lit = waiting.find(last + 1)) != waiting.end()) {
			waiting.erase(lit);
			++last;
		}
	} else {
		waiting.insert(frame);
	}
	if (m_cb)
		m_cb->frameFinished(frame);
}

ostream& lima::SlsDetector::operator <<(ostream& os, Camera::Type type)
{
	const char *name = "Invalid";
	switch (type) {
	case Camera::UnknownDet:	name = "Unknown";	break;
	case Camera::GenericDet:	name = "Generic";	break;
	case Camera::EigerDet:		name = "Eiger";		break;
	case Camera::JungfrauDet:	name = "Jungfrau";	break;
	}
	return os << name;
}

ostream& lima::SlsDetector::operator <<(ostream& os, 
					const Camera::FrameMap& m)
{
	os << "<";
	os << "LastSeqFinishedFrame=" << m.getLastSeqFinishedFrame() << ", "
	   << "NonSeqFinishedFrames=" << m.getNonSeqFinishedFrames() << ", "
	   << "FramePendingItemsMap=" << m.getFramePendingItemsMap();
	return os << ">";
}

ostream& lima::SlsDetector::operator <<(ostream& os, 
					const Camera::FrameMap::List& l)
{
	os << "[";
	typedef Camera::FrameMap::List List;
	List::const_iterator it, end = l.end();
	bool first;
	for (it = l.begin(), first = true; it != end; ++it, first = false)
		os << (first ? "" : ", ") << *it;
	return os << "]";
}

ostream& lima::SlsDetector::operator <<(ostream& os, 
					const Camera::FrameMap::Map& m)
{
	os << "{";
	typedef Camera::FrameMap::Map Map;
	Map::const_iterator it, end = m.end();
	bool first;
	for (it = m.begin(), first = true; it != end; ++it, first = false)
		os << (first ? "" : ", ") << it->first << ": " << it->second;
	return os << "}";
}

Camera::Receiver::FrameFinishedCallback::FrameFinishedCallback(Receiver *r) 
	: m_recv(r)
{
	DEB_CONSTRUCTOR();
}

void Camera::Receiver::FrameFinishedCallback::frameFinished(int frame) 
{
	DEB_MEMBER_FUNCT();
	DEB_RECV_FRAME() << DEB_VAR2(frame, m_recv->m_idx); 
	m_recv->m_cam->receiverFrameFinished(frame, m_recv);
}

Camera::Receiver::Receiver(Camera *cam, int idx, int rx_port, int mode)
	: m_cam(cam), m_idx(idx), m_rx_port(rx_port), 
	  m_mode(mode)
{
	DEB_CONSTRUCTOR();

	m_cb = new FrameFinishedCallback(this);

	m_packet_map.setCallback(m_cb);

	ostringstream os;
	os << "slsReceiver"
	   << " --rx_tcpport " << m_rx_port << " --mode " << m_mode;
	m_args.set(os.str());

	start();

	m_recv->registerCallBackStartAcquisition(startCallback, this);
	m_recv->registerCallBackRawDataReady(frameCallback, this);
}

Camera::Receiver::~Receiver()
{
	DEB_DESTRUCTOR();
	m_recv->stop();
}

void Camera::Receiver::start()
{	
	DEB_MEMBER_FUNCT();
	int init_ret;
	m_recv = new slsReceiverUsers(m_args.size(), m_args, init_ret);
	if (init_ret == slsReceiverDefs::FAIL)
		THROW_HW_ERROR(Error) << "Error creating slsReceiver";
	if (m_recv->start() == slsReceiverDefs::FAIL) 
		THROW_HW_ERROR(Error) << "Error starting slsReceiver";
}

void Camera::Receiver::prepareAcq()
{
	DEB_MEMBER_FUNCT();
	int nb_packets = m_cam->m_model->getRecvFramePackets();
	m_packet_map.setNbItems(nb_packets);
	m_packet_map.clear();
}

int Camera::Receiver::startCallback(char *fpath, char *fname, 
				    int fidx, int dsize, void *priv)
{
	DEB_STATIC_FUNCT();
	Receiver *recv = static_cast<Receiver *>(priv);
	return recv->startCallback(fpath, fname, fidx, dsize);
}

void Camera::Receiver::frameCallback(int frame, char *dptr, int dsize, FILE *f, 
				     char *guidptr, void *priv)
{
	DEB_STATIC_FUNCT();
	Receiver *recv = static_cast<Receiver *>(priv);
	recv->frameCallback(frame, dptr, dsize, f, guidptr);
}

int Camera::Receiver::startCallback(char *fpath, char *fname, 
				    int fidx, int dsize)
{
	DEB_MEMBER_FUNCT();
	Model *model = m_cam->m_model;
	if (!model)
		return DO_NOTHING;
	return model->processRecvStart(m_idx, dsize);
}

void Camera::Receiver::frameCallback(int frame, char *dptr, int dsize, FILE *f, 
				     char *guidptr)
{
	DEB_MEMBER_FUNCT();

	Model *model = m_cam->m_model;
	if (!model || (m_cam->getState() == Stopping))
		return;

	char *bptr = m_cam->getFrameBufferPtr(frame);
	Mutex& lock = m_cam->m_cond.mutex();
	int packet_idx = model->processRecvPacket(m_idx, frame, dptr, dsize, 
						  lock, bptr);
	m_packet_map.frameItemFinished(frame, packet_idx);
}

Camera::FrameFinishedCallback::FrameFinishedCallback(Camera *cam)
	 : m_cam(cam)
{
	DEB_CONSTRUCTOR();
}

void Camera::FrameFinishedCallback::frameFinished(int frame)
{
	DEB_MEMBER_FUNCT();
	m_cam->frameFinished(frame);
}

Camera::AcqThread::AcqThread(Camera *cam)
	: m_cam(cam), m_cond(m_cam->m_cond), m_state(m_cam->m_state),
	  m_frame_queue(m_cam->m_frame_queue)
{
	DEB_CONSTRUCTOR();
	m_state = Starting;
	start();
	while (m_state != Running)
		m_cond.wait();
}

void Camera::AcqThread::stop(bool wait)
{
	DEB_MEMBER_FUNCT();
	m_state = StopReq;
	m_cond.broadcast();
	while (wait && (m_state != Stopped))
		m_cond.wait();
}

void Camera::AcqThread::threadFunction()
{
	DEB_MEMBER_FUNCT();

	AutoMutex l = m_cam->lock();
	{
		AutoMutexUnlock u(l);
		m_cam->putCmd("receiver start");
		m_cam->putCmd("status start");
	}
	m_state = Running;
	m_cond.broadcast();

	do {
		while ((m_state != StopReq) && m_frame_queue.empty())
			m_cond.wait();
		if (!m_frame_queue.empty()) {
			int frame = m_frame_queue.front();
			m_frame_queue.pop();
			bool cont_acq;
			{
				AutoMutexUnlock u(l);
				DEB_TRACE() << DEB_VAR1(frame);
				cont_acq = newFrameReady(frame);
			}
			if (!cont_acq)
				m_state = StopReq;
		}
	} while (m_state != StopReq);

	m_state = Stopping;
	{
		AutoMutexUnlock u(l);
		m_cam->putCmd("status stop");
		m_cam->putCmd("receiver stop");
	}
	m_state = Stopped;
	m_cond.broadcast();
}

bool Camera::AcqThread::newFrameReady(int frame)
{
	DEB_MEMBER_FUNCT();
	HwFrameInfoType frame_info;
	frame_info.acq_frame_nb = frame;
	bool cont_acq = m_cam->m_buffer_cb_mgr->newFrameReady(frame_info);
	return cont_acq && (frame < m_cam->m_nb_frames - 1);
}

Camera::Camera(string config_fname) 
	: m_model(NULL),
	  m_image_type(Bpp16), 
	  m_save_raw(false),
	  m_state(Idle)
{
	DEB_CONSTRUCTOR();

	m_input_data = new AppInputData(config_fname);

	removeSharedMem();
	createReceivers();

	DEB_TRACE() << "Creating the multiSlsDetector object";
	m_det = new multiSlsDetector(0);
	DEB_TRACE() << "Reading configuration file";
	const char *fname = m_input_data->config_file_name.c_str();
	m_det->readConfigurationFile(fname);

	DEB_TRACE() << "Creating the multiSlsDetectorCommand";
	m_cmd = new multiSlsDetectorCommand(m_det);

	setNbFrames(1);
	setExpTime(0.99);
	setFramePeriod(1.0);
}

Camera::~Camera()
{
	DEB_DESTRUCTOR();

	if (!m_model)
		return;

	stopAcq();
	m_model->m_cam = NULL;
}

Camera::Type Camera::getType()
{
	DEB_MEMBER_FUNCT();
	string type_resp = getCmd("type");
	ostringstream os;
	os << "(([^+]+)\\+){" << getNbDetModules() << "}";
	DEB_TRACE() << DEB_VAR1(os.str());
	RegEx re(os.str());
	FullMatch full_match;
	if (!re.match(type_resp, full_match))
		THROW_HW_ERROR(Error) << "Invalid type response: " << type_resp;
	string type_str = full_match[2];
	Type det_type = UnknownDet;
	if (type_str == "Generic") {
		det_type = GenericDet;
	} else if (type_str == "Eiger") {
		det_type = EigerDet;
	} else if (type_str == "Jungfrau") {
		det_type = JungfrauDet;
	}
	DEB_RETURN() << DEB_VAR1(det_type);
	return det_type;
}

void Camera::setModel(Model *model)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR2(model, m_model);
	
	if (model && (model->getType() != getType()))
		THROW_HW_ERROR(InvalidValue) << DEB_VAR2(getType(), 
							 model->getType());
	if (m_model == model)
		return;
	if (m_model)
		m_model->m_cam = NULL;
	m_model = model;
}

char *Camera::getFrameBufferPtr(int frame_nb)
{
	DEB_MEMBER_FUNCT();

	StdBufferCbMgr *cb_mgr = m_buffer_cb_mgr;
	if (!cb_mgr)
		THROW_HW_ERROR(InvalidValue) << "No BufferCbMgr defined";
	void *ptr = cb_mgr->getFrameBufferPtr(frame_nb);
	return static_cast<char *>(ptr);
}

void Camera::removeSharedMem()
{
	DEB_MEMBER_FUNCT();
	const char *cmd = "ipcs -m | "
		"grep -E '^0x000016[0-9a-z]{2}' | "
		"awk '{print $2}' | while read m; do ipcrm -m $m; done";
	system(cmd);
}

void Camera::createReceivers()
{
	DEB_MEMBER_FUNCT();

	DEB_TRACE() << "Receivers:";
	const RecvPortMap& recv_port_map = m_input_data->recv_port_map;
	RecvPortMap::const_iterator mit, mend = recv_port_map.end();
	int idx = 0;
	for (mit = recv_port_map.begin(); mit != mend; ++mit, ++idx) {
		unsigned int id = mit->first;
		if (id >= m_input_data->host_name_list.size())
			THROW_HW_FATAL(InvalidValue) << DEB_VAR1(id) 
						     << "too high";
		const string& host_name = m_input_data->host_name_list[id];
		int rx_port = mit->second;
		int mode = (id % 2);
		DEB_TRACE() << "  " << host_name << ": " 
			    << DEB_VAR2(rx_port, mode);

		AutoPtr<Receiver> recv_obj = new Receiver(this, idx, rx_port, 
							  mode);
		m_recv_list.push_back(recv_obj);
	}

	m_frame_cb = new FrameFinishedCallback(this);
	m_recv_map.setNbItems(recv_port_map.size());
	m_recv_map.setCallback(m_frame_cb);
}

void Camera::putCmd(const string& s, int idx)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << "s=\"" << s << "\"";
	Args args(s);
	AutoMutex l(m_cmd_mutex);
	m_cmd->putCommand(args.size(), args, idx);
}

string Camera::getCmd(const string& s, int idx)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << "s=\"" << s << "\"";
	Args args(s);
	AutoMutex l(m_cmd_mutex);
	string r = m_cmd->getCommand(args.size(), args, idx);
	DEB_RETURN() << "r=\"" << r << "\"";
	return r;
}

void Camera::setNbFrames(int nb_frames)
{
	DEB_MEMBER_FUNCT();
	putCmd("timing auto");
	putNbCmd<int>("frames", nb_frames);
	m_nb_frames = nb_frames;
}

void Camera::getNbFrames(int& nb_frames)
{
	DEB_MEMBER_FUNCT();
	nb_frames = m_nb_frames;
}

void Camera::setExpTime(double exp_time)
{
	DEB_MEMBER_FUNCT();
	putNbCmd<double>("exptime", exp_time);
	m_exp_time = exp_time;
}

void Camera::getExpTime(double& exp_time)
{ 
	DEB_MEMBER_FUNCT();
	exp_time = m_exp_time;
}

void Camera::setFramePeriod(double frame_period)
{
	DEB_MEMBER_FUNCT();
	putNbCmd<double>("period", frame_period);
	m_frame_period = frame_period;
}

void Camera::getFramePeriod(double& frame_period)
{
	DEB_MEMBER_FUNCT();
	frame_period = m_frame_period;
}

State Camera::getState()
{
	DEB_MEMBER_FUNCT();
	AutoMutex l = lock();
	State state = getEffectiveState();
	DEB_RETURN() << DEB_VAR1(state);
	return state;
}

State Camera::getEffectiveState()
{
	if (m_state == Stopped) {
		m_acq_thread = NULL;
		m_state = Idle;
	}
	return m_state;
}

void Camera::waitState(State state)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR1(state);
	AutoMutex l = lock();
	while (getEffectiveState() != state)
		m_cond.wait();
}

State Camera::waitNotState(State state)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR1(state);
	AutoMutex l = lock();
	while (getEffectiveState() == state)
		m_cond.wait();
	state = getEffectiveState();
	DEB_RETURN() << DEB_VAR1(state);
	return state;
}

void Camera::prepareAcq()
{
	DEB_MEMBER_FUNCT();

	if (!m_buffer_cb_mgr)
		THROW_HW_ERROR(Error) << "No BufferCbMgr defined";
	if (!m_model)
		THROW_HW_ERROR(Error) << "No BufferCbMgr defined";

	waitNotState(Stopping);
	if (getState() != Idle)
		THROW_HW_ERROR(Error) << "Camera is not idle";

	{
		AutoMutex l = lock();
		RecvList::iterator it, end = m_recv_list.end();
		for (it = m_recv_list.begin(); it != end; ++it)
			(*it)->prepareAcq();
		m_recv_map.clear();
		DEB_TRACE() << DEB_VAR1(m_frame_queue.size());
		while (!m_frame_queue.empty())
			m_frame_queue.pop();
	}

	putCmd("resetframescaught");
}

void Camera::startAcq()
{
	DEB_MEMBER_FUNCT();

	AutoMutex l = lock();
	if (m_acq_thread)
		THROW_HW_ERROR(Error) << "Must call prepareAcq first";

	m_buffer_cb_mgr->setStartTimestamp(Timestamp::now());

	m_acq_thread = new AcqThread(this);
}

void Camera::stopAcq()
{
	DEB_MEMBER_FUNCT();

	AutoMutex l = lock();
	if (getEffectiveState() != Running)
		return;

	m_acq_thread->stop(true);
	if (getEffectiveState() != Idle)
		THROW_HW_ERROR(Error) << "Camera not Idle";
}

void Camera::receiverFrameFinished(int frame, Receiver *recv)
{
	DEB_MEMBER_FUNCT();
	DEB_RECV_FRAME() << DEB_VAR2(frame, recv->m_idx);
	AutoMutex l = lock();
	m_recv_map.frameItemFinished(frame, recv->m_idx);
}

void Camera::frameFinished(int frame)
{
	DEB_MEMBER_FUNCT();
	DEB_CAMERA_FRAME() << DEB_VAR1(frame);

	m_frame_queue.push(frame);
	m_cond.broadcast();
}

int Camera::getFramesCaught()
{
	DEB_MEMBER_FUNCT();
	int frames_caught = getNbCmd<int>("framescaught");
	DEB_RETURN() << DEB_VAR1(frames_caught);
	return frames_caught;
}

string Camera::getStatus()
{
	DEB_MEMBER_FUNCT();
	return getCmd("status");
}

void Camera::setHighVoltage(int hvolt)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR1(hvolt);
	putNbCmd<int>("vhighvoltage", hvolt);
}

void Camera::getHighVoltage(int& hvolt)
{
	DEB_MEMBER_FUNCT();
	hvolt = getNbCmd<int>("vhighvoltage");
	DEB_RETURN() << DEB_VAR1(hvolt);
}

void Camera::setEnergyThreshold(int thres)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR1(thres);
	putNbCmd<int>("threshold", thres);
}

void Camera::getEnergyThreshold(int& thres)
{
	DEB_MEMBER_FUNCT();
	thres = getNbCmd<int>("threshold");
	DEB_RETURN() << DEB_VAR1(thres);
}

const int Eiger::ChipSize = 256;
const int Eiger::ChipGap = 2;
const int Eiger::HalfModuleChips = 4;

Eiger::ChipBorderCorrBase::ChipBorderCorrBase(Eiger *eiger)
	: m_eiger(eiger)
{
	DEB_CONSTRUCTOR();
}

void Eiger::ChipBorderCorrBase::prepareAcq()
{
	DEB_MEMBER_FUNCT();

	m_nb_modules = m_eiger->m_nb_det_modules / 2;
	Size recv_size = m_eiger->m_recv_frame_dim.getSize();
	m_mod_size = recv_size * Point(1, 2);
	m_size = m_mod_size * Point(1, m_nb_modules);
	m_inter_lines.resize(m_nb_modules);
	for (int i = 0; i < m_nb_modules - 1; ++i) {
		m_inter_lines[i] = m_eiger->getInterModuleGap(i);
		m_size += Point(0, m_inter_lines[i]);
	}
	m_inter_lines[m_nb_modules - 1] = 0;
}

Eiger::ChipBorderCorrBase::~ChipBorderCorrBase()
{
	DEB_DESTRUCTOR();
	if (m_eiger)
		m_eiger->m_corr = NULL;
}

Eiger::Correction::Correction(Eiger *eiger)
{
	DEB_CONSTRUCTOR();

	eiger->getCamera()->getSaveRaw(m_raw);
	ImageType image_type = eiger->getCamera()->getImageType();
	m_corr = eiger->createChipBorderCorr(image_type);
}

Eiger::Correction::~Correction()
{
	DEB_DESTRUCTOR();
}

Data Eiger::Correction::process(Data& data)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR4(data.frameNumber, m_raw, 
				_processingInPlaceFlag, data.data());
	
	Data ret = data;

	if (!_processingInPlaceFlag) {
		int size = data.size();
		Buffer *buffer = new Buffer(size);
		memcpy(buffer->data, data.data(), size);
		ret.setBuffer(buffer);
		buffer->unref();
	}

	if (!m_raw)
		m_corr->correctFrame(ret.frameNumber, ret.data());

	return ret;
}

Eiger::Eiger(Camera *cam)
	: Camera::Model(cam, Camera::EigerDet), m_corr(NULL)
{
	DEB_CONSTRUCTOR();

	m_nb_det_modules = getCamera()->getNbDetModules();
	DEB_TRACE() << "Using Eiger detector, " << DEB_VAR1(m_nb_det_modules);
}

Eiger::~Eiger()
{
	DEB_DESTRUCTOR();
	if (m_corr)
		m_corr->m_eiger = NULL;
}

int Eiger::getPacketLen()
{
	DEB_MEMBER_FUNCT();
	int packet_len = sizeof(Packet);
	DEB_RETURN() << DEB_VAR1(packet_len);
	return packet_len;
}

int Eiger::getRecvFramePackets()
{
	DEB_MEMBER_FUNCT();
	FrameDim frame_dim;
	getRecvFrameDim(frame_dim, false, false);
	int frame_packets = frame_dim.getMemSize() / sizeof(Packet::data);
	DEB_RETURN() << DEB_VAR1(frame_packets);
	return frame_packets;
}

void Eiger::getFrameDim(FrameDim& frame_dim, bool raw)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR1(raw);
	getRecvFrameDim(frame_dim, raw, true);
	Size size = frame_dim.getSize();
	size *= Point(1, m_nb_det_modules);
	if (!raw)
		for (int i = 0; i < m_nb_det_modules / 2 - 1; ++i)
			size += Point(0, getInterModuleGap(i));
	frame_dim.setSize(size);
	DEB_RETURN() << DEB_VAR1(frame_dim);
}

void Eiger::getRecvFrameDim(FrameDim& frame_dim, bool raw, bool geom)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR1(raw);
	if (raw) {
		frame_dim.setImageType(Bpp8);
		frame_dim.setSize(Size(getPacketLen(), getRecvFramePackets()));
	} else {
		frame_dim.setImageType(getCamera()->getImageType());
		Size size(ChipSize, ChipSize);
		size *= Point(HalfModuleChips, 2);
		if (geom)
			size += Point(ChipGap, ChipGap) * Point(3, 1);
		frame_dim.setSize(size / Point(1, 2));
	}
	DEB_RETURN() << DEB_VAR1(frame_dim);
}

string Eiger::getName()
{
	DEB_MEMBER_FUNCT();
	ostringstream os;
	os << "PSI/Eiger-";
	if (m_nb_det_modules == 1) {
		os << "500K";
	} else if (m_nb_det_modules == 8) {
		os << "2M";
	} else {
		os << m_nb_det_modules << "-Modules";
	}
	string name = os.str();
	DEB_RETURN() << DEB_VAR1(name);
	return name;
}

void Eiger::getPixelSize(double& x_size, double& y_size)
{
	DEB_MEMBER_FUNCT();
	x_size = y_size = 75e-6;
	DEB_RETURN() << DEB_VAR2(x_size, y_size);
}

int Eiger::processRecvStart(int recv_idx, int dsize)
{
	DEB_MEMBER_FUNCT();
	DEB_CAMERA_START() << DEB_VAR2(recv_idx, dsize);
	if (dsize != getPacketLen())
		DEB_WARNING() << "!!!! Warning !!!! " 
			      << DEB_VAR2(dsize, getPacketLen());

	if (recv_idx == 0) {
		getCamera()->getSaveRaw(m_raw);
		getRecvFrameDim(m_recv_frame_dim, m_raw, true);
		m_recv_half_frame_packets = getRecvFramePackets() / 2;

		DEB_TRACE() << DEB_VAR3(m_raw, m_recv_frame_dim, 
					m_recv_half_frame_packets);

		if (m_corr)
			m_corr->prepareAcq();
	}

	return DO_NOTHING;
}

int Eiger::processRecvPacket(int recv_idx, int frame, char *dptr, int dsize,
			     Mutex& lock, char *bptr)
{
	DEB_MEMBER_FUNCT();

	Packet *p = static_cast<Packet *>(static_cast<void *>(dptr));
	bool second_half = p->pre.flags & 0x20;
	int half_packet_idx = p->pre.idx;
	int packet_idx = second_half ? m_recv_half_frame_packets : 0;
	packet_idx += half_packet_idx;
	bool top_half_recv = (recv_idx % 2 == 0);

	DEB_RECV_PACKET() << DEB_VAR4(frame, dsize, recv_idx, packet_idx);

	const FrameDim& frame_dim = m_recv_frame_dim;
	const Size& size = frame_dim.getSize();
	int depth = frame_dim.getDepth();
	int dlw = size.getWidth() * depth;		// dest line width
	int recv_size = frame_dim.getMemSize();
	char *dest = bptr + recv_size * recv_idx;	

	if (m_raw) {
		dest += dlw * packet_idx;
		memcpy(dest, dptr, dlw);
	} else {
		int mod_idx = recv_idx / 2;
		for (int i = 0; i < mod_idx; ++i)
			dest += getInterModuleGap(i) * dlw;
		int plw = ChipSize * depth;		// packet line width
		int pchips = HalfModuleChips / 2;
		int plines = sizeof(p->data) / plw / pchips;
		if (top_half_recv) {
			dest += (ChipSize - 1) * dlw;
			dlw *= -1;
		} else {
			dest += dlw * (ChipGap / 2);
		}
		dest += half_packet_idx * plines * dlw;
		int clw = plw + ChipGap * depth;	// chip line width
		bool right_side = (second_half == top_half_recv);
		dest += right_side ? pchips * clw : 0;
		char *src = p->data;
		for (int i = 0; i < plines; ++i, dest += dlw) {
			char *d = dest;
			for (int j = 0; j < pchips; ++j, src += plw, d += clw)
				memcpy(d, src, plw);
		}
	}

	return packet_idx;
}

Eiger::Correction *Eiger::createCorrectionTask()
{
	DEB_MEMBER_FUNCT();
	return new Correction(this);
}

Eiger::ChipBorderCorrBase *Eiger::createChipBorderCorr(ImageType image_type)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR1(image_type);

	ChipBorderCorrBase *corr;
	switch (image_type) {
	case Bpp8:
		corr = new ChipBorderCorr<Byte>(this);
		break;
	case Bpp16:
		corr = new ChipBorderCorr<Word>(this);
		break;
	case Bpp32:
		corr = new ChipBorderCorr<Long>(this);
		break;
	default:
		THROW_HW_ERROR(NotSupported) 
			<< "Eiger correction not supported for " << image_type;
	}

	if (m_corr)
		m_corr->m_eiger = NULL;
	m_corr = corr;

	return corr;
}

double Eiger::getBorderCorrFactor(int det, int line)
{
	DEB_MEMBER_FUNCT();
	switch (line) {
	case 0: return 2.0;
	case 1: return 1.3;
	default: return 1;
	}
}

int Eiger::getInterModuleGap(int det)
{
	DEB_MEMBER_FUNCT();
	if (det >= m_nb_det_modules - 1)
		THROW_HW_ERROR(InvalidValue) << "Invalid " << DEB_VAR1(det);
	return 35;
}

