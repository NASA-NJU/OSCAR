/*
 * Copyright (c) 2023
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
 * Author: Zhaochen Zhang (zhaochen.zhang@outlook.com)
 */

#ifndef ROCEV2_OSCAR_H
#define ROCEV2_OSCAR_H

#include "rocev2-congestion-ops.h"

#include "ns3/data-rate.h"
#include "ns3/queue-size.h"
#include "ns3/random-variable-stream.h"
#include "ns3/rocev2-header.h"
#include "ns3/string.h"

#include <deque>
#include <map>
#include <tuple>
#include <vector>

namespace ns3
{

class RoCEv2SocketState;

class OscarBatchedEstimator
{
  public:
    OscarBatchedEstimator()
    {
    }

    OscarBatchedEstimator(Time duration, uint32_t pktSize)
        : m_sumX(0),
          m_sumY(0),
          m_sumXX(0),
          m_sumXY(0),
          m_k(0),
          m_duration(duration),
          m_sumRate(0),
          m_nodes(0),
          m_tsUpdate(0),
          m_k3(0),
          m_oracleLastAvgRate(0),
          m_wndStart(0),
          m_wndEnd(0),
          m_pktSize(pktSize)
    {
    }

    // Push one (sendTs, delay) sample into the current window. Returns true when the
    // window has just closed and Calculate() has been invoked on its samples.
    bool Update(double sendTs, double delay, double recvTs, double rate)
    {
        m_sumX += sendTs;
        m_sumY += delay;
        m_sumXX += sendTs * sendTs;
        m_sumXY += sendTs * delay;
        m_sumRate += rate;
        m_nodes++;

        if (m_tsUpdate == 0)
        {
            m_tsUpdate = sendTs + m_duration.GetNanoSeconds();
            m_wndStart = sendTs;
        }

        m_points.push_back(std::make_tuple(sendTs, delay));

        if (sendTs > m_tsUpdate && m_nodes >= m_nMinWindowSize)
        {
            m_wndEnd = sendTs;
            Calculate();

            m_tsUpdate = sendTs + m_duration.GetNanoSeconds();
            m_wndStart = sendTs;
            return true;
        }
        return false;
    }

    // Calculate the delay gradient using least squares
    void Calculate()
    {
        double denom = m_nodes * m_sumXX - m_sumX * m_sumX;
        if (denom != 0)
        {
            m_k = (m_nodes * m_sumXY - m_sumX * m_sumY) / denom;
        }
        else
        {
            m_k = 0;
        }
        m_avgDelay = m_sumY / m_nodes;

        m_lastAvgRate = m_sumRate / m_nodes;

        m_oracleLastAvgRate =
            m_nodes * m_pktSize * 8 / ((m_wndEnd - m_wndStart) / 1e9) / m_lineRate;

        // Discard windows much wider than the configured duration: such windows mostly
        // appear during idle gaps and produce noisy gradients.
        if (m_wndEnd - m_wndStart > m_duration.GetNanoSeconds() * 3)
        {
            m_k = 0;
            m_oracleLastAvgRate = 0;
        }

        // [EXPERIMENTAL] Three-point BLS variant: keep only the first, middle, and last
        // samples of the window and run LS over those three points to produce m_k3.
        // Not part of the algorithm in the paper (paper's BLS uses all samples in the
        // window). This is used to mimic ACK coalescing (Figure 21b & Figure 25).
        if (m_points.size() >= 3)
        {
            double sumX = 0.0, sumY = 0.0, sumXX = 0.0, sumXY = 0.0;
            while (m_points.size() > 4)
            {
                m_points.erase(m_points.begin() + 1);
                m_points.erase(m_points.end() - 2);
            }
            if (m_points.size() == 4)
            {
                m_points.erase(m_points.begin() + 1);
            }
            for (uint32_t j = 0; j < 3; ++j)
            {
                sumX += std::get<0>(m_points[j]);
                sumY += std::get<1>(m_points[j]);
                sumXX += std::get<0>(m_points[j]) * std::get<0>(m_points[j]);
                sumXY += std::get<0>(m_points[j]) * std::get<1>(m_points[j]);
            }
            double denom = 3 * sumXX - sumX * sumX;
            m_k3 = (3 * sumXY - sumX * sumY) / denom;
            m_points.clear();
        }
    }

    double GetK() const
    {
        return m_k;
    }

    double GetK3() const
    {
        return m_k3;
    }

    double GetAvgDelay() const
    {
        return m_avgDelay;
    }

    void Reset()
    {
        m_sumX = 0;
        m_sumY = 0;
        m_sumXX = 0;
        m_sumXY = 0;
        m_sumRate = 0;
        m_nodes = 0;
        m_tsUpdate = 0;
    }

    double GetAverageRateRatio()
    {
        return m_lastAvgRate;
    }

    double GetOracleAverageRateRatio()
    {
        return m_oracleLastAvgRate;
    }

    // All members are public for simplicity.
    uint32_t m_nMinWindowSize = 3;
    double m_sumX, m_sumY, m_sumXX, m_sumXY;
    double m_k;
    double m_avgDelay;
    Time m_duration;
    double m_sumRate;
    uint32_t m_nodes;
    double m_lastAvgRate;
    double m_tsUpdate;

    // Buffer of (sendTs, delay) samples used by the three-point LS variant.
    std::vector<std::tuple<double, double>> m_points;
    double m_k3;

    double m_oracleLastAvgRate; //!< Window-average rate ratio computed from bytes sent / window length.
    double m_wndStart;          //!< Start of the current delay-gradient window
    double m_wndEnd;            //!< End of the current delay-gradient window
    uint32_t m_pktSize;         //!< MTU in bytes
    double m_lineRate = 100e9;  //!< Line rate in bps
}; // class OscarBatchedEstimator

class RoCEv2Oscar : public RoCEv2CongestionOps
{
  public:
    /**
     * Get the type ID.
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId(void);

    RoCEv2Oscar();
    RoCEv2Oscar(Ptr<RoCEv2SocketState> sockState);
    ~RoCEv2Oscar();

    /**
     * After configuring the Timely, call this function.
     */
    void SetReady() override;

    /**
     * When the sender sending out a packet, add a SeqTsHeader into packet, in order to store
     * timestamp.
     */
    void UpdateStateSend(Ptr<Packet> packet) override;

    /**
     * When the receiver generating an ACK, move the SeqTsHeade from packet to ack.
     */
    void UpdateStateWithGenACK(Ptr<Packet> packet, Ptr<Packet> ack) override;

    /**
     * When the sender receiving an ACK.
     */
    void UpdateStateWithRcvACK(Ptr<Packet> ack,
                               const RoCEv2Header& roce,
                               const uint32_t senderNextPSN) override;

    std::string GetName() const override;

    class Stats : public RoCEv2CongestionOps::Stats
    {
      public:
        Stats();

        // Detailed statistics, only enabled if needed
        bool bDetailedSenderStats;
        std::vector<std::tuple<Time, Time, Time>>
            vPacketDelay; //!< The Delay masurement per packet, recorded as send time, recv time and
                          //!< delay
        std::vector<std::tuple<Time, Time, double>>
            vPacketDelayGradientBls; //!< The Delay masurement per packet calculated by BLS
        std::vector<std::tuple<Time, Time, double>>
            vPacketDelayGradientBls3; //!< The Delay masurement per packet calculated by BLS

        // Complete statistics, record per BLS calculation, and only enabled if BLS is enabled
        struct OscarCompleteStats
        {
            Time tNow;                 //!< Current time
            Time tDelay;               //!< The delay of the packet
            Time tBlsWndBegin;         //!< The begin of the BLS window
            Time tBlsWndEnd;           //!< The end of the BLS window
            double delayGradient;      //!< The delay gradient calculated by BLS
            double blsRefRateRatio;    //!< The reference rate ratio by BLS
            double oracleRefRateRatio; //!< The oracle reference rate ratio
            double updatedRateRatio;   //!< The updated rate ratio
        };

        std::vector<OscarCompleteStats> vOscarCompleteStats; //!< The complete statistics of the
                                                             //!< Oscar complete event

        // Recorder function of the detailed statistics
        void RecordPacketDelay(Time sendTime, Time delay);
        void RecordPacketDelayGradientBls(Time sendTime, double delayGradient);
        void RecordPacketDelayGradientBls3(Time sendTime, double delayGradient);
        // To avoid copy, use rvalue reference
        void RecordOscarCompleteStats(OscarCompleteStats&& stats);
        void RecordOscarCompleteStatsRateRatio(double updatedRateRatio);

        // Collect the statistics and check if the statistics is correct
        void CollectAndCheck();

        // No getter for simplicity
    };

    std::shared_ptr<RoCEv2CongestionOps::Stats> GetStats() const;

  private:
    /**
     * Initialize the state.
     */
    void Init();

    // <psn, sendTs, sendRate (in rateRatio), window ratio>
    typedef std::tuple<uint32_t, uint64_t, double, double> PktInfo;

    /**
     * \brief Do calculation when a delay measurement is received
     * \return Whether the gradient is caculated or not
     */
    bool DoDelayGradientCalculation(PktInfo ackedPkt, Time delay);

    /**
     * \brief Do rate update with a scale factor
     */
    void DoRateUpdate(double refenceCwnd,
                      double refenceRate,
                      double delayGradient,
                      double delay,
                      double& curRateRatio);

    void ResetRttRecord();

    /**
     * \brief Set target queue length in bytes
     */
    void SetTargetQueueLengthInBytes(StringValue targetQueueLength);
    Time ConvertBytesToTime(QueueSize bytes);

    /**
     * \brief Set rate ratio, overwrite the function in RoCEv2CongestionOps
     */
    void SetRateRatio(double rateRatio);
    /**
     * \brief Set cwnd, overwrite the function in RoCEv2CongestionOps
     */
    void SetCwnd(uint32_t cwnd);

    std::shared_ptr<Stats> m_stats; //!< Statistics

    uint32_t m_lastUpdateSeq; //!< lastUpdateSeq to record RTT.

    // [EXPERIMENTAL] Two structures for tracking in-flight packets. DEQUE assumes
    // in-order ACKs (matches the paper's assumption); MAP is keyed by PSN to tolerate
    // ACK reordering (e.g., per-packet load-balancing experiments). Not discussed in
    // the paper; selectable via the RttRecordStructure attribute.
    enum RttRecordStructure
    {
        DEQUE,
        MAP
    };

    std::deque<PktInfo> m_inflightPkts;            //!< In-flight packet records (FIFO order)
    std::map<uint32_t, PktInfo> m_inflightPktsMap; //!< In-flight packet records keyed by PSN
    RttRecordStructure m_rttRecordStructure;       //!< The structure to record RTT.

    Time m_tLastDelay;                       //!< Last delay
    Time m_tLastPktSendTime;                 //!< Last packet's send time
    Time m_targetThreshold;                  //!< Target delay threshold (base + target queue length)
    QueueSize m_targetQueueLengthInBytes;    //!< Target queue length in bytes

    OscarBatchedEstimator m_bls;           //!< Batched least squares for delay gradient
    Time m_blsDuration;                    //!< Duration for batched least squares

    std::map<double, double> m_delayErrorCdf;   //!< CDF of delay error
    Ptr<UniformRandomVariable> m_rngDelayError; //!< rng to choose delay error from the cdf

    double m_raiRatio;
    double m_linearStartRatio;

    // [EXPERIMENTAL] If true, BLS uses only first/middle/last samples (m_k3) instead
    // of the full-window least squares (m_k). Ablation knob; the paper uses m_k.
    bool m_threePointBls;

    double WindowRatio();           //!< Calculate the window ratio
    uint32_t m_inflightSize;        //!< [EXPERIMENTAL] Tracked but unused; left for future stats hooks.

    // [EXPERIMENTAL] Delay-signal selector. The paper uses RTT (sender-side timestamp
    // diff) exclusively. The OWD path piggybacks a receiver-side timestamp via
    // OscarOwdHeader for ablation experiments comparing OWD vs RTT signals; m_rttCorrection
    // converts the OWD-based threshold back to an RTT-equivalent.
    enum SignalType
    {
        OWD,
        RTT
    };

    SignalType m_signalType; //!< Signal type
    Time m_rttCorrection;    //!< Used to correct OWD to RTT when OWD is used
    Time GetBase();          //!< Get the base time (base OWD or base RTT)
    void SetSignalType(enum SignalType signalType);

    // [EXPERIMENTAL] Decouples the window-control loop from the rate-control loop:
    // the window is driven by u_w (delay-window) while the rate is driven by u_r
    // (gradient-rate), instead of the paper's unified ratio u that drives both.
    // Not part of the published design; kept as an ablation alternative.
    bool m_windowRateDecouple;
    double m_windowRatio;      //!< Window ratio used when m_windowRateDecouple is true
}; // class RoCEv2Oscar

// [EXPERIMENTAL] Header carrying a receiver-side timestamp (recvTs), used only when
// SignalType==OWD so the sender can compute one-way delay. The paper uses RTT and
// does not require this header.
class OscarOwdHeader : public Header
{
    // Oscar OWD header with only one timestamp.
  public:
    OscarOwdHeader()
        : m_recvTs(Simulator::Now().GetNanoSeconds())
    {
    }

    static TypeId GetTypeId(void)
    {
        static TypeId tid = TypeId("ns3::OscarOwdHeader")
                                .SetParent<Header>()
                                .SetGroupName("Dcb")
                                .AddConstructor<OscarOwdHeader>();
        return tid;
    }

    TypeId GetInstanceTypeId(void) const override
    {
        return GetTypeId();
    }

    inline uint32_t GetSerializedSize(void) const override
    {
        return 4;
    }

    void Serialize(Buffer::Iterator start) const override
    {
        Buffer::Iterator i = start;
        i.WriteHtonU32(m_recvTs);
    }

    uint32_t Deserialize(Buffer::Iterator start) override
    {
        Buffer::Iterator i = start;
        m_recvTs = i.ReadNtohU32();
        return GetSerializedSize();
    }

    void Print(std::ostream& os) const override
    {
        os << "time=" << NanoSeconds(m_recvTs).As(Time::S) << "";
    }

    Time GetTs() const
    {
        return NanoSeconds(m_recvTs);
    }

    uint32_t m_recvTs; //!< Timestamp
};

class OscarBaseHeader : public Header
{
    // Oscar base header with send timestamp and inflight state.
  public:
    OscarBaseHeader()
        : m_sendTs(Simulator::Now().GetNanoSeconds()),
          m_inflight(0)
    {
    }

    OscarBaseHeader(uint16_t inflight)
        : m_sendTs(Simulator::Now().GetNanoSeconds()),
          m_inflight(inflight)
    {
    }

    static TypeId GetTypeId(void)
    {
        static TypeId tid = TypeId("ns3::OscarBaseHeader")
                                .SetParent<Header>()
                                .SetGroupName("Dcb")
                                .AddConstructor<OscarBaseHeader>();
        return tid;
    }

    TypeId GetInstanceTypeId(void) const override
    {
        return GetTypeId();
    }

    inline uint32_t GetSerializedSize(void) const override
    {
        return 6;
    }

    void Serialize(Buffer::Iterator start) const override
    {
        Buffer::Iterator i = start;
        i.WriteHtonU32(m_sendTs);
        i.WriteHtonU16(m_inflight);
    }

    uint32_t Deserialize(Buffer::Iterator start) override
    {
        Buffer::Iterator i = start;
        m_sendTs = i.ReadNtohU32();
        m_inflight = i.ReadNtohU16();
        return GetSerializedSize();
    }

    void Print(std::ostream& os) const override
    {
        os << "time=" << NanoSeconds(m_sendTs).As(Time::S) << "";
    }

    Time GetTs() const
    {
        return NanoSeconds(m_sendTs);
    }

    uint32_t m_sendTs;   //!< Timestamp
    uint16_t m_inflight; //!< Inflight
};

} // namespace ns3

#endif // ROCEV2_OSCAR_H
