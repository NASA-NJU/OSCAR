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
 * Author: Pavinberg <pavin0702@gmail.com>
 */

#ifndef PPFC_FRAME_H
#define PPFC_FRAME_H

#include "ns3/header.h"
#include "ns3/packet.h"

namespace ns3
{

class PPfcFrame : public Header
{
  public:
    constexpr static const uint16_t PROT_NUMBER = 0x8809;

    PPfcFrame();

    static TypeId GetTypeId();
    virtual TypeId GetInstanceTypeId(void) const override;
    virtual uint32_t GetSerializedSize() const override;
    virtual void Serialize(Buffer::Iterator start) const override;
    virtual uint32_t Deserialize(Buffer::Iterator start) override;
    virtual void Print(std::ostream& os) const override;

    static Ptr<Packet> GeneratePauseFrame(uint32_t qIdx, uint32_t type, uint32_t cate);

    uint32_t GetQIdx() const;
    uint32_t GetType() const;
    uint32_t GetCategory() const;

    void SetQIdx(uint32_t qIdx);
    void SetType(uint32_t type);
    void SetCategory(uint32_t cate);

  private:
    uint32_t m_qIdx; // the Q this packet to pause / resume.
    uint32_t m_type; // 0 for pause; 1 for resume.
    uint32_t m_category; // the category of congested Q.(DcTopology::SwicthFCCateory::NeedRelay / DcTopology::SwicthFCCateory::NxtToDst).
};

} // namespace ns3

#endif // PFC_FRAME_H
