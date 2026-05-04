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
 * Author: F.Y. Xue <xue.fyang@foxmail.com>
 */

#ifndef ROCEV2_POWERTCP_H
#define ROCEV2_POWERTCP_H

#include "rocev2-congestion-ops.h"

#include "ns3/data-rate.h"
#include "ns3/hpcc-header.h"
#include "ns3/rocev2-header.h"

#include <deque>

namespace ns3
{

class RoCEv2SocketState;

/**
 * The PowerTCP implementation according to paper:
 *   Vamsi Addanki, et al. "PowerTCP: Pushing the Performance Limits of Datacenter Networks.
 *   \url https://www.usenix.org/conference/nsdi22/presentation/addanki
 */

class RoCEv2Powertcp : public RoCEv2CongestionOps
{
  public:
    /**
     * Get the type ID.
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    RoCEv2Powertcp();
    RoCEv2Powertcp(Ptr<RoCEv2SocketState> sockState);
    ~RoCEv2Powertcp() override;

    /**
     * After configuring the PowerTCP, call this function.
     */
    void SetReady() override;

    /**
     * When the sender sending out a packet, add an empty HPCC header into packet.
     */
    virtual void UpdateStateSend(Ptr<Packet> packet) override;

    /**
     * When the receiver generating an ACK, move the HPCC header from packet to ack.
     */
    virtual void UpdateStateWithGenACK(Ptr<Packet> packet, Ptr<Packet> ack) override;

    /**
     * When the sender receiving an ACK, do what Powertcp does.
     */
    void UpdateStateWithRcvACK(Ptr<Packet> ack,
                               const RoCEv2Header& roce,
                               const uint32_t senderNextPSN) override;

    std::string GetName() const override;

    class Stats : public RoCEv2CongestionOps::Stats
    {
      public:
        // constructor
        Stats();

        // Detailed statistics, only enabled if needed
        bool bDetailedSenderStats;
        std::deque<std::pair<uint32_t, Time>> m_inflightPkts; //!< sendTs to record RTT.
        std::vector<std::tuple<Time, Time, Time>>
            vPacketDelay; //!< The Delay masurement per packet, recorded as send time, recv time and
                          //!< delay
        std::vector<std::pair<Time, double>> vPower; //!< The power measurement per packet

        // Recorder function of the detailed statistics
        void RecordPacketSend(uint32_t seq, Time sendTs);
        void RecordPacketDelay(uint32_t seq); // Called when receiving an ACK
        void RecordPower(double power);

        // Collect the statistics and check if the statistics is correct
        void CollectAndCheck();

        // No getter for simplicity
    };

    std::shared_ptr<RoCEv2CongestionOps::Stats> GetStats() const override;

  protected:
    /**
     * \brief Calculate the normalized power.
     */
    virtual void NormPower(Ptr<Packet> ack, const uint32_t ackSeq);

    /**
     * \brief Calculate new cwnd based on the normalized power and old cwnd.
     */
    void UpdateWindow();

    /**
     * \brief Update the old cwnd. And for PowerTcp-INT, Copy the INT feedbacks.
     * In a word, this function does Algorithm 1:Line 7 in paper.
     */
    virtual void UpdateOld(Ptr<Packet> ack, const uint32_t ackSeq, const uint32_t senderNextPSN);

    double m_gamma;           //!< γ in paper. EWMA parameter.
    double m_raiRatio;        //!< RateAI ratio. Additive increase parameter.
    uint32_t m_beta;          // !< β in paper. Calculated from raiRatio.
    uint32_t m_lastUpdateSeq; //!< lastUpdateSeq, to make sure that cwnd is updated only once per
                              //!< RTT.
    uint64_t m_oldCwnd;       //!< cwnd_{old} in paper.

    double m_power; //!< The smoothed normalized power. Initialized as 1.0.

    bool m_isTheta;   //!< True if is theta-powerTcp.
    bool m_canUpdate; //!< Whether the cwnd can be updated.

    bool m_arrivalRateBound; //!< Whether the arrival rate is bounded.
    bool m_deltaTBound;      //!< Whether the deltaTNorm is bounded.
    bool m_gammaOld;         //!< Whether use old cwnd in gamma update.

  private:
    /**
     * Initialize the state.
     */
    void Init();
    std::shared_ptr<Stats> m_stats; //!< Statistics

    void CopyIntHop(const IntHop* src, uint32_t nhop);
    IntHop m_hops[HpccHeader::MAX_HOP]; //!< prevInt in paper, storing link feedbacks

}; // class RoCEv2Powertcp

class RoCEv2ThetaPowertcp : public RoCEv2Powertcp
{
  public:
    static TypeId GetTypeId();

    RoCEv2ThetaPowertcp();
    RoCEv2ThetaPowertcp(Ptr<RoCEv2SocketState> sockState);
    ~RoCEv2ThetaPowertcp() override;

    /**
     * theta recovers a timeslot into the map.
     */
    virtual void UpdateStateSend(Ptr<Packet> packet) override;

    /**
     * theta does nothing.
     */
    virtual void UpdateStateWithGenACK(Ptr<Packet> packet, Ptr<Packet> ack) override
    {
    }

    std::string GetName() const override;

  protected:
    void NormPower(Ptr<Packet> ack, const uint32_t ackSeq) override;
    void UpdateOld(Ptr<Packet> ack, const uint32_t ackSeq, const uint32_t senderNextPSN) override;

  private:
    /**
     * Initialize the state.
     */
    void Init();
    std::shared_ptr<Stats> m_stats; //!< Statistics

    Time m_prevTc;                    //!< t_c^{prev} in paper.
    Time m_prevRtt;                   //!< prevRTT in paper.
    std::map<uint32_t, Time> m_tsMap; //!< To store timeslot of each PSN.

}; // class RoCEv2ThetaPowertcp

} // namespace ns3

#endif // POWERTCP_H
