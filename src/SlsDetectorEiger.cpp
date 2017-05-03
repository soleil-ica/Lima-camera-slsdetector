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

#include "SlsDetectorEiger.h"

using namespace std;
using namespace lima;
using namespace lima::SlsDetector;

const int Eiger::ChipSize = 256;
const int Eiger::ChipGap = 2;
const int Eiger::HalfModuleChips = 4;
const int Eiger::RecvPorts = 2;

Eiger::CorrBase::CorrBase(Eiger *eiger, CorrType type)
	: m_eiger(eiger), m_type(type)
{
	DEB_CONSTRUCTOR();
	m_nb_modules = m_eiger->m_nb_det_modules / 2;
}

Eiger::CorrBase::~CorrBase()
{
	DEB_DESTRUCTOR();
	if (m_eiger)
		m_eiger->removeCorr(this);
}

bool Eiger::CorrBase::getRaw()
{
	DEB_MEMBER_FUNCT();
	bool raw;
	m_eiger->getCamera()->getRawMode(raw);
	DEB_RETURN() << DEB_VAR1(raw);
	return raw; 
}

void Eiger::CorrBase::prepareAcq()
{
	DEB_MEMBER_FUNCT();

	if (!m_eiger)
		THROW_HW_ERROR(InvalidValue) << "Correction already removed";

	FrameDim& recv_dim = m_eiger->m_recv_frame_dim;
	m_mod_frame_dim = recv_dim * Point(1, 2);
	m_frame_size = m_mod_frame_dim.getSize() * Point(1, m_nb_modules);
	m_inter_lines.resize(m_nb_modules);
	for (int i = 0; i < m_nb_modules - 1; ++i) {
		m_inter_lines[i] = m_eiger->getInterModuleGap(i);
		m_frame_size += Point(0, m_inter_lines[i]);
	}
	m_inter_lines[m_nb_modules - 1] = 0;
}

Eiger::InterModGapCorr::InterModGapCorr(Eiger *eiger)
	: CorrBase(eiger, Gap)
{
	DEB_CONSTRUCTOR();
}

void Eiger::InterModGapCorr::prepareAcq()
{
	DEB_MEMBER_FUNCT();

	CorrBase::prepareAcq();

	m_gap_list.clear();

	int mod_size = m_mod_frame_dim.getMemSize();
	int width = m_mod_frame_dim.getSize().getWidth();
	int dlw = width * m_mod_frame_dim.getDepth();
	for (int i = 0, start = 0; i < m_nb_modules - 1; ++i) {
		start += mod_size;
		int size = m_inter_lines[i] * dlw;
		m_gap_list.push_back(Block(start, size));
		start += size;
	}
}

void Eiger::InterModGapCorr::correctFrame(FrameType frame, void *ptr)
{
	DEB_MEMBER_FUNCT();
	
	char *dest = static_cast<char *>(ptr);
	BlockList::const_iterator it, end = m_gap_list.end();
	for (it = m_gap_list.begin(); it != end; ++it) {
		int start = it->first;
		int size = it->second;
		memset(dest + start, 0, size);
	}
}

Eiger::Correction::Correction(Eiger *eiger)
{
	DEB_CONSTRUCTOR();

	CorrBase *corr;
	ImageType image_type = eiger->getCamera()->getImageType();
	corr = eiger->createChipBorderCorr(image_type);
	m_corr_list.push_back(corr);

	if (corr->getNbModules() > 1) {
		corr = eiger->createInterModGapCorr();
		m_corr_list.push_back(corr);
	}
}

Data Eiger::Correction::process(Data& data)
{
	DEB_MEMBER_FUNCT();

	CorrBase *corr = m_corr_list[0];
	bool raw = corr->getRaw();
	DEB_PARAM() << DEB_VAR4(data.frameNumber, raw, 
				_processingInPlaceFlag, data.data());
	
	Data ret = data;

	if (!_processingInPlaceFlag) {
		int size = data.size();
		Buffer *buffer = new Buffer(size);
		memcpy(buffer->data, data.data(), size);
		ret.setBuffer(buffer);
		buffer->unref();
	}

	if (!raw) {
		FrameType frame = ret.frameNumber;
		void *ptr = ret.data();
		CorrList::iterator it, end = m_corr_list.end();
		for (it = m_corr_list.begin(); it != end; ++it)
			(*it)->correctFrame(frame, ptr);
	}
	return ret;
}

Eiger::RecvPortGeometry::RecvPortGeometry(Eiger *eiger, int recv_idx, int port)
	: m_eiger(eiger), m_port(port), m_recv_idx(recv_idx)
{
	DEB_CONSTRUCTOR();
	DEB_PARAM() << DEB_VAR1(m_recv_idx);
	m_top_half_recv = (m_recv_idx % 2 == 0);
}

void Eiger::RecvPortGeometry::prepareAcq()
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR1(m_recv_idx);

	const FrameDim& frame_dim = m_eiger->m_recv_frame_dim;
	const Size& size = frame_dim.getSize();
	int depth = frame_dim.getDepth();
	m_dlw = size.getWidth() * depth;
	int recv_size = frame_dim.getMemSize();
	m_port_offset = recv_size * m_recv_idx;	

	m_eiger->getCamera()->getRawMode(m_raw);
	if (m_raw) {
		m_port_offset += ChipSize * m_dlw * m_port;
		return;
	}

	int mod_idx = m_recv_idx / 2;
	for (int i = 0; i < mod_idx; ++i)
		m_port_offset += m_eiger->getInterModuleGap(i) * m_dlw;

	if (m_top_half_recv) {
		m_port_offset += (ChipSize - 1) * m_dlw;
		m_dlw *= -1;
	} else {
		m_port_offset += (ChipGap / 2) * m_dlw;
	}

	m_plw = ChipSize * depth;
	m_pchips = HalfModuleChips / RecvPorts;
	m_clw = m_plw + ChipGap * depth;
	m_port_offset += m_pchips * m_clw * m_port;
}

void Eiger::RecvPortGeometry::processRecvFileStart(uint32_t dsize)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR2(m_recv_idx, dsize);
}

void Eiger::RecvPortGeometry::processRecvPort(FrameType frame, char *dptr,
					      char *bptr)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR3(frame, m_recv_idx, m_port);

	char *dest = bptr + m_port_offset;	
	if (m_raw) {
		memcpy(dest, dptr, m_dlw * ChipSize);
		return;
	}
	
	char *src = dptr;
	for (int i = 0; i < ChipSize; ++i, dest += m_dlw) {
		char *d = dest;
		for (int j = 0; j < m_pchips; ++j, src += m_plw, d += m_clw)
			memcpy(d, src, m_plw);
	}
}

Eiger::Eiger(Camera *cam)
	: Camera::Model(cam, Camera::EigerDet)
{
	DEB_CONSTRUCTOR();

	m_nb_det_modules = getCamera()->getNbDetModules();
	DEB_TRACE() << "Using Eiger detector, " << DEB_VAR1(m_nb_det_modules);

	for (int i = 0; i < m_nb_det_modules; ++i) {
		for (int j = 0; j < RecvPorts; ++j) {
			RecvPortGeometry *g = new RecvPortGeometry(this, i, j);
			m_port_geom_list.push_back(g);
		}
	}
}

Eiger::~Eiger()
{
	DEB_DESTRUCTOR();
	CorrMap::iterator it, end = m_corr_map.end();
	for (it = m_corr_map.begin(); it != end; ++it)
		removeCorr(it->second);
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
	frame_dim.setImageType(getCamera()->getImageType());
	Size chip_size(ChipSize, ChipSize);
	if (raw) {
		int port_chips = HalfModuleChips / RecvPorts;
		frame_dim.setSize(chip_size * Point(port_chips, RecvPorts));
	} else {
		Size size = chip_size * Point(HalfModuleChips, 2);
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
	int nb_modules = m_nb_det_modules / 2;
	if (nb_modules == 1) {
		os << "500k";
	} else if (nb_modules == 4) {
		os << "2M";
	} else {
		os << nb_modules << "-Modules";
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

bool Eiger::checkSettings(Settings settings)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR1(settings);
	bool ok;
	switch (settings) {
	case Defs::Standard:
		ok = true;
		break;
	default:
		ok = false;
	}

	DEB_RETURN() << DEB_VAR1(ok);
	return ok;
}

int Eiger::getRecvPorts()
{
	DEB_MEMBER_FUNCT();
	DEB_RETURN() << DEB_VAR1(RecvPorts);
	return RecvPorts;
}

void Eiger::prepareAcq()
{
	DEB_MEMBER_FUNCT();

	bool raw;
	getCamera()->getRawMode(raw);
	getRecvFrameDim(m_recv_frame_dim, raw, true);
	
	DEB_TRACE() << DEB_VAR2(raw, m_recv_frame_dim);

	for (int i = 0; i < m_nb_det_modules * RecvPorts; ++i)
		m_port_geom_list[i]->prepareAcq();

	CorrMap::iterator it, end = m_corr_map.end();
	for (it = m_corr_map.begin(); it != end; ++it) {
		CorrBase *corr = it->second;
		corr->prepareAcq();
	}
}

void Eiger::processRecvFileStart(int recv_idx, uint32_t dsize)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR2(recv_idx, dsize);
	for (int i = 0; i < RecvPorts; ++i) {
		int port_idx = getPortIndex(recv_idx, i);
		m_port_geom_list[port_idx]->processRecvFileStart(dsize);
	}
}

void Eiger::processRecvPort(int recv_idx, FrameType frame, int port, char *dptr, 
			    uint32_t dsize, Mutex& lock, char *bptr)
{
	DEB_MEMBER_FUNCT();
	int port_idx = getPortIndex(recv_idx, port);
	m_port_geom_list[port_idx]->processRecvPort(frame, dptr, bptr);
}

Eiger::Correction *Eiger::createCorrectionTask()
{
	DEB_MEMBER_FUNCT();
	return new Correction(this);
}

Eiger::CorrBase *Eiger::createChipBorderCorr(ImageType image_type)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR1(image_type);

	CorrBase *border_corr;
	switch (image_type) {
	case Bpp8:
		border_corr = new ChipBorderCorr<Byte>(this);
		break;
	case Bpp16:
		border_corr = new ChipBorderCorr<Word>(this);
		break;
	case Bpp32:
		border_corr = new ChipBorderCorr<Long>(this);
		break;
	default:
		THROW_HW_ERROR(NotSupported) 
			<< "Eiger correction not supported for " << image_type;
	}

	addCorr(border_corr);

	DEB_RETURN() << DEB_VAR1(border_corr);
	return border_corr;
}

Eiger::CorrBase *Eiger::createInterModGapCorr()
{
	DEB_MEMBER_FUNCT();
	CorrBase *gap_corr = new InterModGapCorr(this);
	addCorr(gap_corr);
	DEB_RETURN() << DEB_VAR1(gap_corr);
	return gap_corr;
}

void Eiger::addCorr(CorrBase *corr)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR1(corr);

	CorrMap::iterator it = m_corr_map.find(corr->m_type);
	if (it != m_corr_map.end())
		removeCorr(it->second);
	m_corr_map[corr->m_type] = corr;
}

void Eiger::removeCorr(CorrBase *corr)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR1(corr);

	CorrMap::iterator it = m_corr_map.find(corr->m_type);
	if (it == m_corr_map.end()) {
		DEB_WARNING() << DEB_VAR1(corr) << " already removed";
		return;
	}

	corr->m_eiger = NULL;
	m_corr_map.erase(it);
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
	if (det >= (m_nb_det_modules / 2) - 1)
		THROW_HW_ERROR(InvalidValue) << "Invalid " << DEB_VAR1(det);
	return 35;
}
