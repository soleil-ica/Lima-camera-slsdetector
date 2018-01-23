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

// namespace SlsDetector
// {
// typedef std::map<PixelDepth, SystemCPUAffinity> PixelDepthCPUAffinityMap;
// };

%MappedType SlsDetector::PixelDepthCPUAffinityMap
{
%TypeHeaderCode
#include <map>
#include "SlsDetectorCPUAffinity.h"
#include "sipAPIlimaslsdetector.h"

using namespace std;
using namespace lima;
using namespace lima::SlsDetector;

typedef PixelDepth CPP_KeyType;
typedef SystemCPUAffinity CPP_ValueType;
typedef PixelDepthCPUAffinityMap CPP_MapType;
#define SIP_ValueType sipType_SlsDetector_SystemCPUAffinity
%End

%ConvertToTypeCode
	if (sipIsErr == NULL) {
		bool ok = PyDict_Check(sipPy);
		PyObject *key, *value;
		SIP_SSIZE_T pos = 0;
		while (ok && PyDict_Next(sipPy, &pos, &key, &value))
			ok = (PyInt_Check(key) && 
			      sipCanConvertToType(value, SIP_ValueType, 
						  SIP_NOT_NONE));
		return ok;
	}

	CPP_MapType *cpp_map = new CPP_MapType();

	PyObject *key, *value;
	SIP_SSIZE_T pos = 0;
	while (PyDict_Next(sipPy, &pos, &key, &value)) {
		CPP_KeyType cpp_key = CPP_KeyType(PyInt_AS_LONG(key));
		int state;
		void *p = sipConvertToType(value, SIP_ValueType, NULL, 
					   SIP_NOT_NONE, &state, sipIsErr);
		CPP_ValueType *cpp_value = static_cast<CPP_ValueType *>(p);
		if (*sipIsErr) {
			sipReleaseType(cpp_value, SIP_ValueType, state);
			delete cpp_map;
			return 0;
		}		   

		CPP_MapType::value_type map_value(cpp_key, *cpp_value);
		cpp_map->insert(map_value);
		sipReleaseType(cpp_value, SIP_ValueType, state);
	}
	*sipCppPtr = cpp_map;

	return sipGetState(sipTransferObj);
%End

%ConvertFromTypeCode
	PyObject* sip_map = PyDict_New();

	CPP_MapType::iterator it, end = sipCpp->end();
	for (it = sipCpp->begin();  it != end; ++it) {
		PyObject *key = PyInt_FromLong(it->first);
		if (key == NULL)
			goto error;
		CPP_ValueType *new_val = new CPP_ValueType(it->second);
		PyObject *value = sipConvertFromNewType(new_val, SIP_ValueType,
							NULL);
		if (value == NULL) {
			delete new_val;
			Py_DECREF(key);
			goto error;
		}
		PyDict_SetItem(sip_map, key, value);
		Py_DECREF(value);
		Py_DECREF(key);
	}
	return sip_map;
error:
	Py_DECREF(sip_map);
	return NULL;
%End
};


namespace SlsDetector
{

%TypeHeaderCode
#include "SlsDetectorCamera.h"
%End

class CPUAffinity 
{
public:
	CPUAffinity(unsigned long m = 0);

	static bool UseSudo;
	static int getNbCPUs(bool max_nb = false);
	static unsigned long allCPUs(bool max_nb = false);

	//void initCPUSet(cpu_set_t& cpu_set) const;
	void applyToTask(int task, bool incl_threads = true,
			 bool use_taskset = true) const;

	operator unsigned long() const;

	//CPUAffinity& operator =(unsigned long m);

	bool isDefault() const;

	static std::string getProcDir(bool local_threads);
	static std::string getTaskProcDir(int task, bool is_thread);
};

class ProcCPUAffinityMgr
{
public:
	ProcCPUAffinityMgr();
	~ProcCPUAffinityMgr();

	enum Filter {
		All, MatchAffinity, NoMatchAffinity, ThisProc=0x10
	};

	static std::vector<int> getProcList(
		SlsDetector::ProcCPUAffinityMgr::Filter filter
			= SlsDetector::ProcCPUAffinityMgr::All,
		SlsDetector::CPUAffinity cpu_affinity = 0);
	static std::vector<int> getThreadList(
		SlsDetector::ProcCPUAffinityMgr::Filter filter
			= SlsDetector::ProcCPUAffinityMgr::All,
		SlsDetector::CPUAffinity cpu_affinity = 0);

	void setOtherCPUAffinity(
		SlsDetector::CPUAffinity affinity);
};

struct SystemCPUAffinity {
	SlsDetector::CPUAffinity recv;
	SlsDetector::CPUAffinity lima;
	SlsDetector::CPUAffinity other;
};

// typedef std::map<SlsDetector::PixelDepth, SlsDetector::SystemCPUAffinity> 
//						PixelDepthCPUAffinityMap;

class SystemCPUAffinityMgr
{
public:
	class ProcessingFinishedEvent
	{
	public:
		ProcessingFinishedEvent(SlsDetector::SystemCPUAffinityMgr *mgr);
		~ProcessingFinishedEvent();

		void processingFinished();

		void registerStatusCallback(CtControl *ct_control);
	};

	SystemCPUAffinityMgr(SlsDetector::Camera *cam = NULL);
	~SystemCPUAffinityMgr();
	
	void applyAndSet(const SlsDetector::SystemCPUAffinity& o);
	void updateRecvRestart();

	SlsDetector::SystemCPUAffinityMgr::
	ProcessingFinishedEvent *getProcessingFinishedEvent();

	void prepareAcq();
	void startAcq();
	void stopAcq();
	void recvFinished();
	void limaFinished();
	void waitLimaFinished();
};


}; // namespace SlsDetector