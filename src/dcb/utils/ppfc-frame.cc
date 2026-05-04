/*
 * Copyright (c) 2008 INRIA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: ChangLiu <liuchang_1307@163.com>
 */

#include "ppfc-frame.h"

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(PPfcFrame);

PPfcFrame::PPfcFrame()
    : m_qIdx(0),
      m_type(0),
      m_category(1000)
{
}

TypeId
PPfcFrame::GetTypeId()
{
    static TypeId tid = TypeId("ns3::PPfcFrame").SetParent<Header>().AddConstructor<PPfcFrame>();
    return tid;
}

TypeId
PPfcFrame::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
PPfcFrame::GetSerializedSize() const
{
    constexpr uint32_t sz = sizeof(m_qIdx) + sizeof(m_type) + sizeof(m_category);
    return sz;
}

void
PPfcFrame::Serialize(Buffer::Iterator start) const
{
    start.WriteHtonU32(m_qIdx);
    start.WriteHtonU32(m_type);
    start.WriteHtonU32(m_category);
}

uint32_t
PPfcFrame::Deserialize(Buffer::Iterator start)
{
    m_qIdx = start.ReadNtohU32();
    m_type = start.ReadNtohU32();
    m_category = start.ReadNtohU32();
    return GetSerializedSize();
}

void
PPfcFrame::Print(std::ostream& os) const
{
    os << "PPFC frame: pause/resume qIdx: " << m_qIdx << " frame type is " << m_type <<" frame category is "<<m_category<< std::endl;
}

// static
Ptr<Packet>
PPfcFrame::GeneratePauseFrame(uint32_t qIdx, uint32_t type, uint32_t cate)
{
    PPfcFrame pPfcFrame;
    pPfcFrame.SetQIdx(qIdx);
    pPfcFrame.SetType(type);
    pPfcFrame.SetCategory(cate);

    Ptr<Packet> packet = Create<Packet>(0);
    packet->AddHeader(pPfcFrame);
    return packet;
}

uint32_t
PPfcFrame::GetQIdx() const
{
    return m_qIdx;
}

uint32_t
PPfcFrame::GetType() const
{
    return m_type;
}

uint32_t
PPfcFrame::GetCategory() const
{
    return m_category;
}

void
PPfcFrame::SetQIdx(uint32_t qIdx)
{
    m_qIdx = qIdx;
}

void
PPfcFrame::SetType(uint32_t type)
{
    NS_ASSERT(type == 0 || type == 1);
    m_type = type;
}

void
PPfcFrame::SetCategory(uint32_t cate)
{
    NS_ASSERT(cate < 2); // now we only have two catories to generate pause/resume frames.
    m_category = cate;
}

} // namespace ns3
