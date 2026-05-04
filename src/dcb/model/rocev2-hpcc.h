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

#ifndef ROCEV2_HPCC_H
#define ROCEV2_HPCC_H

#include "rocev2-congestion-ops.h"

#include "ns3/data-rate.h"
#include "ns3/hpcc-header.h"
#include "ns3/rocev2-header.h"

#include <deque>

namespace ns3
{

class RoCEv2SocketState;

/**
 * The HPCC implementation according to paper:
 *   Li, Yuliang, et al. "HPCC: high precision congestion control." ACM SIGCOMM.
 *   \url https://dl.acm.org/doi/10.1145/3341302.3342085
 */
class RoCEv2Hpcc : public RoCEv2CongestionOps
{
  public:
    /**
     * Get the type ID.
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    RoCEv2Hpcc();
    RoCEv2Hpcc(Ptr<RoCEv2SocketState> sockState);
    ~RoCEv2Hpcc() override;

    /**
     * After configuring the HPCC, call this function.
     */
    void SetReady() override;

    /**
     * When the sender sending out a packet, add an empty HPCC header into packet.
     */
    void UpdateStateSend(Ptr<Packet> packet) override;

    /**
     * When the receiver generating an ACK, move the HPCC header from packet to ack.
     */
    void UpdateStateWithGenACK(Ptr<Packet> packet, Ptr<Packet> ack) override;

    /**
     * When the sender receiving an ACK, do what HPCC does.
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
        std::vector<std::pair<Time, double>>
            vU; //!< The inflight utilization measurement per packet

        // Recorder function of the detailed statistics
        void RecordPacketSend(uint32_t seq, Time sendTs);
        void RecordPacketDelay(uint32_t seq); // Called when receiving an ACK
        void RecordU(double u);

        // Collect the statistics and check if the statistics is correct
        void CollectAndCheck();

        // No getter for simplicity
    };

    std::shared_ptr<RoCEv2CongestionOps::Stats> GetStats() const override;

  private:
    /**
     * \brief Measure the current inflight.
     */
    void MeasureInflight(const HpccHeader& hpccHeader);

    /**
     * \brief Update the current rate.
     *
     * Note: we use the same formula as in paper's simulation, which is a bit different from the
     * paper's Algorithm 1. That is, we use U to calculate the current rate instead of the current
     * window, and then we use the current rate to update the current window.
     *
     * This function behaves like ComputeWind() in paper's Algorithm 1.
     */
    void UpdateRate(bool updateCurrent);

    void CopyIntHop(const IntHop* src, uint32_t nhop);

    /**
     * Initialize the state.
     */
    void Init();

    void SetUString(std::string uStr);

    std::shared_ptr<Stats> m_stats; //!< Statistics

    uint32_t m_lastUpdateSeq; //!< lastUpdateSeq in paper.

    uint32_t m_incStage; //!< incStage in paper.
    uint32_t m_maxStage; //!< maxStage in paper. Default to 5.

    double m_cRateRatio; //!< rate-based WC in paper.

    double m_raiRatio; //!< RateAI / link rate for additive increase

    double m_targetUtil; //!< η in paper. Default to 0.95.

    double m_u; //!< U in paper. Current inflight utilization.

    IntHop m_hops[HpccHeader::MAX_HOP]; //!< L in paper, storing link feedbacks

    /***** Members for experiment mode *****/
    bool m_experimentMode; //!< Whether to enable the experiment mode.
    std::vector<std::pair<Time, double>>
        m_experimentStates; //!< Experiment states. A vector of <time, rateRatio>
    /**
     * \brief Update the experiment state.
     * \param idx The index of the experiment state.
     */
    void UpdateExperimentState(uint32_t idx);
    bool m_immediateMode; //!< Whether to enable the immediate mode, no EWMA for u 
    bool m_refFixMode; //!< Whether to fix the refrence rate 
    std::map<uint32_t, double> m_refMap; //!< The reference rate map
    double WindowRatio(); //!< The ratio of window-based rate to rate-based rate

    static inline std::vector<std::pair<Time, double>> longFlowStates = {
        {Seconds(0.1), 1.0},
        {Seconds(0.1005) + Time("50us"), 0.2},
        // {Seconds(0.1005) + Time("120us"), 0.1},
        {Seconds(0.1015) + Time("50us"), 1.}
    };
    static inline std::vector<std::pair<Time, double>> shortFlowStates = {
        {Seconds(0.1005), 1.0},
        {Seconds(0.1005) + Time("50us"), 0.2},
        // {Seconds(0.1005) + Time("120us"), 0.1},
        {Seconds(0.1015), 0.}
    };

}; // class RoCEv2Hpcc

} // namespace ns3

#endif // HPCC_H
