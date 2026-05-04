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

#ifndef DCB_PPFC_PORT_H
#define DCB_PPFC_PORT_H

#include "dcb-flow-control-port.h"
#include "dcb-traffic-control.h"

#include "ns3/event-id.h"
#include "ns3/net-device.h"

namespace ns3
{
// the flow control config in a port
struct DcbPPfcPortConfig
{
    // give the off and on threshol in a category
    // (in per-port-fc we classify the queues into two category, so we have two sets of off and on
    // threshold)
    struct ThresholdConfig
    {
        uint32_t category, xoff, xon;

        ThresholdConfig(uint32_t cate, uint32_t off, uint32_t on)
            : category(cate),
              xoff(off),
              xon(on)
        {
        }
    }; // struct QueueConfig

    void AddThresholdConfig(uint32_t cate, uint32_t off, uint32_t on)
    {
        categories.emplace_back(cate, off, on);
    }

    uint32_t port;
    uint8_t enableVec = 0xff;
    std::vector<ThresholdConfig> categories;
    uint32_t qReservedSize;
}; // struct DcbPPfcPortConfig

class DcbPPfcPort : public DcbFlowControlPort
{
  public:
    static TypeId GetTypeId();

    DcbPPfcPort(Ptr<NetDevice> dev, Ptr<DcbTrafficControl> tc);
    virtual ~DcbPPfcPort();

    void ReceivePfc(Ptr<NetDevice> device,
                    Ptr<const Packet> p,
                    uint16_t protocol,
                    const Address& from,
                    const Address& to,
                    NetDevice::PacketType packetType);

    void AddCategory(uint32_t xoff, uint32_t xon);

    bool CheckShouldSendPause(uint32_t cate) const;

    bool CheckShouldSendResume(uint32_t cate) const;

    virtual void DoIngressProcess(Ptr<NetDevice> outDev, Ptr<QueueDiscItem> item) override;
    /**
     * \brief Process when a packet previously came from this port is going to send
     * out though other port.
     */
    virtual void DoPacketOutCallbackProcess(uint32_t priority, Ptr<Packet> packet) override;
    virtual void DoEgressProcess(Ptr<Packet> packet) override;
    /**
     * \brief called by tc to send a pause(type=0)/resume(type=1).
     * \param cate the category of congested queues.(DcTopology::SwicthFCCateory::NeedRelay /
     * DcTopology::SwicthFCCateory::NxtToDst).
     * \param pauseQIdx the index of Queue that the packet
     * should pause/resume (equal to the congested port's index).
     * \param from the Ipv4Address of the port that send this packet.
     * \param type the type of packet. 0 for pause; 1 for resume.
     */
    void DoSendPause(uint32_t cate, uint32_t pauseQIdx, const Address& from, uint32_t type);

    /**
     * \brief called by tc to send a relay pause(type=0)/resume(type=1). this func different from
     * DoSendPause in this func don't call  SetUpstreamPaused()
     * \param cate the category of congested queues.(DcTopology::SwicthFCCateory::NxtToDst).
     * \param pauseQIdx the index of Queue that the packet should pause/resume (equal to the
     * congested port's index). \param from the Ipv4Address of the port that send this packet.
     * \param type the type of packet. 0 for pause; 1 for resume.
     */
    void DoSendRelayPause(uint32_t cate, uint32_t pauseQIdx, const Address& from, uint32_t type);
    void SetUpstreamPaused(uint32_t cate, bool paused);

  protected:
    struct EgressPortInfo
    {
        struct EgressCategoryInfo
        {
            uint32_t xoff;
            uint32_t xon;
            bool isUpstreamPaused; // liuchangTODO: how to check if already paused upstream?

            EventId pauseEvent;
            EgressCategoryInfo();

            EgressCategoryInfo(uint32_t off, uint32_t on)
                : xoff(off),
                  xon(on),
                  isUpstreamPaused(false)
            {
            }
        }; // struct EgressCategoryInfo

        // liuchangTODO
        explicit EgressPortInfo(uint32_t index);

        const EgressCategoryInfo& getCategory(uint32_t cate) const;
        EgressCategoryInfo& getCategory(uint32_t cate);
        void AddEgressCategory(uint32_t off, uint32_t on);

        uint32_t m_index;
        std::vector<EgressCategoryInfo> m_egressCategories;
    }; // struct EgressPortInfo

  private:
    std::pair<uint32_t, uint32_t> GetNodeAndPortId() const;

    EgressPortInfo m_egressPort;

    enum PFCExpireReactionType
    {
        RESEND_PAUSE,
        RESET_UPSTREAM_PAUSED,
        NEVER_EXPIRE
    }; // enum PFCExpireReactionType
    const enum PFCExpireReactionType m_reactionType = NEVER_EXPIRE;

    /// Traced callback: fired a PFC frame is sent, trace with node and port id, category, pause or
    /// resume
    TracedCallback<std::pair<uint32_t, uint32_t>, uint32_t, bool> m_tracePfcSent;
    /// Traced callback: fired when a pause frame is received, trace with node and port id,
    /// priority, pause or resume
    TracedCallback<std::pair<uint32_t, uint32_t>, uint32_t, bool> m_tracePfcReceived;

}; // class DcbPPfcPort

} // namespace ns3

#endif // DCB_PPFC_PORT_H
