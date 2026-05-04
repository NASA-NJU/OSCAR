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

#include "rocev2-hpcc.h"

#include "rocev2-socket.h"

#include "ns3/global-value.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoCEv2Hpcc");

NS_OBJECT_ENSURE_REGISTERED(RoCEv2Hpcc);

TypeId
RoCEv2Hpcc::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::RoCEv2Hpcc")
            .SetParent<RoCEv2CongestionOps>()
            .AddConstructor<RoCEv2Hpcc>()
            .SetGroupName("Dcb")
            .AddAttribute("TargetUtil",
                          "HPCC's target utilization",
                          DoubleValue(0.95),
                          MakeDoubleAccessor(&RoCEv2Hpcc::m_targetUtil),
                          MakeDoubleChecker<double>())
            .AddAttribute("TargetUtilString",
                          "HPCC's target utilization",
                          StringValue("0.95u"),
                          MakeStringAccessor(&RoCEv2Hpcc::SetUString),
                          MakeStringChecker())
            .AddAttribute("MaxStage",
                          "HPCC's maximum stage",
                          UintegerValue(5),
                          MakeUintegerAccessor(&RoCEv2Hpcc::m_maxStage),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("RateAIRatio",
                          "HPCC's RateAI ratio",
                          DoubleValue(0.0005),
                          MakeDoubleAccessor(&RoCEv2Hpcc::m_raiRatio),
                          MakeDoubleChecker<double>())
            .AddAttribute("ExperimentMode",
                          "Whether to enable experiment mode",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2Hpcc::m_experimentMode),
                          MakeBooleanChecker())
            .AddAttribute("StartRefRateRatio",
                          "HPCC's start reference rate ratio",
                          DoubleValue(1),
                          MakeDoubleAccessor(&RoCEv2Hpcc::m_cRateRatio),
                          MakeDoubleChecker<double>())
            .AddAttribute("ImmediateMode",
                          "Whether to enable the immediate mode, i.e., no EWMA for u ",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2Hpcc::m_immediateMode),
                          MakeBooleanChecker())
            .AddAttribute("RefFixMode",
                          "Whether to fix the refrence rate ",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2Hpcc::m_refFixMode),
                          MakeBooleanChecker());
    return tid;
}

RoCEv2Hpcc::RoCEv2Hpcc()
    : RoCEv2CongestionOps(std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
//   m_alphaTimer(Timer::CANCEL_ON_DESTROY)
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2Hpcc::RoCEv2Hpcc(Ptr<RoCEv2SocketState> sockState)
    : RoCEv2CongestionOps(sockState, std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
//   m_alphaTimer(Timer::CANCEL_ON_DESTROY)
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2Hpcc::~RoCEv2Hpcc()
{
    NS_LOG_FUNCTION(this);
}

void
RoCEv2Hpcc::SetReady()
{
    NS_LOG_FUNCTION(this);
    // Reload any config before starting
    SetLimiting(true);
    SetRateRatio(m_startRateRatio);

    if (m_experimentMode)
    {
        if (Simulator::GetContext() == 0)
        {
            m_experimentStates = longFlowStates;
        }
        else
        {
            m_experimentStates = shortFlowStates;
        }
        Simulator::Schedule(m_experimentStates[0].first - Simulator::Now(),
                            &RoCEv2Hpcc::UpdateExperimentState,
                            this,
                            0);
    }
}

void
RoCEv2Hpcc::UpdateStateSend(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);
    // Rmove the roceheader
    RoCEv2Header roceHeader;
    packet->RemoveHeader(roceHeader);
    // Add an empty HPCC header into packet
    HpccHeader hpccHeader;
    packet->AddHeader(hpccHeader);
    // Add the roceheader back
    packet->AddHeader(roceHeader);

    // Record the packet send time, used to calc the RTT
    m_stats->RecordPacketSend(roceHeader.GetPSN(), Simulator::Now());

    if (m_refFixMode)
    {
        m_refMap[roceHeader.GetPSN()] = m_sockState->GetRateRatioPercent();
    }
}

double
RoCEv2Hpcc::WindowRatio()
{
    return m_sockState->GetTxBuffer()->InflightPkts() * m_sockState->GetPacketSize() /
           ((double)m_sockState->GetBaseBdp());
}

void
RoCEv2Hpcc::UpdateStateWithGenACK(Ptr<Packet> packet, Ptr<Packet> ack)
{
    NS_LOG_FUNCTION(this << packet << ack);
    // HPCC should remove the HPCC header of the packet.
    HpccHeader hpccHeader;
    packet->RemoveHeader(hpccHeader);
    // And then copy to the ACK.
    // Note that the ACK has RoCEv2Header and AETHeader. And we should add HPCC header between them.
    RoCEv2Header roceHeader;
    ack->RemoveHeader(roceHeader);
    ack->AddHeader(hpccHeader);
    ack->AddHeader(roceHeader);
}

void
RoCEv2Hpcc::UpdateStateWithRcvACK(Ptr<Packet> ack,
                                  const RoCEv2Header& roce,
                                  const uint32_t senderNextPSN)
{
    NS_LOG_FUNCTION(this << ack << roce);
    // HPCC should remove the HPCC header of the ACK.
    HpccHeader hpccHeader;
    ack->RemoveHeader(hpccHeader);
    const IntHop* inthop = hpccHeader.m_intHops;
    uint32_t nhop = hpccHeader.m_nHop;

    uint32_t ackSeq = roce.GetPSN();

    if (m_refFixMode)
    {
        m_cRateRatio = m_refMap[ackSeq];
    }

    if (m_lastUpdateSeq != 0)
    {
        // not first ACK
        MeasureInflight(hpccHeader);
        bool updateCurrent = ackSeq > m_lastUpdateSeq;
        UpdateRate(updateCurrent);
        if (updateCurrent)
        {
            m_lastUpdateSeq = senderNextPSN;
        }
    }
    else
    {
        // first ACK
        m_lastUpdateSeq = senderNextPSN;
    }
    CopyIntHop(inthop, nhop);

    // Record the packet delay
    m_stats->RecordPacketDelay(ackSeq);
}

void
RoCEv2Hpcc::MeasureInflight(const HpccHeader& hpccHeader)
{
    double u = 0;
    Time tau;
    Time baseRtt = m_sockState->GetBaseRtt();
    for (uint32_t i = 0; i < hpccHeader.m_nHop; i++) // Algorithm Line 3
    {
        Time tauPrime = NanoSeconds(hpccHeader.m_intHops[i].GetTimeDelta(m_hops[i]));
        double txRate = (hpccHeader.m_intHops[i].GetBytesDelta(m_hops[i]) * 8.0) /
                        tauPrime.GetSeconds(); // Algorithm Line 4
        double uPrime = txRate / hpccHeader.m_intHops[i].GetLineRate().GetBitRate();
        uPrime += std::min(hpccHeader.m_intHops[i].GetQlen(), m_hops[i].GetQlen()) * 8.0 /
                  baseRtt.GetSeconds() /
                  hpccHeader.m_intHops[i].GetLineRate().GetBitRate(); // Algorithm Line 5

        if (uPrime > u) // Algorithm Line 6
        {
            u = uPrime;
            tau = tauPrime; // Algorithm Line 7
        }
    }
    if (tau > baseRtt) // Algorithm Line 8
    {
        tau = baseRtt;
    }
    double frab = tau.GetSeconds() / baseRtt.GetSeconds();
    m_u = m_u * (1.0 - frab) + u * frab; // Algorithm Line 9
    if (m_immediateMode)
    {
        m_u = u;
    }

    m_stats->RecordU(u);
}

void
RoCEv2Hpcc::UpdateRate(bool updateCurrent)
{
    if (m_experimentMode)
    {
        return;
    }

    double uNormal = m_u / m_targetUtil;
    double newRateRatio;
    uint32_t newIncStage;
    if (uNormal >= 1.0 || m_incStage >= m_maxStage) // Algorithm Line 12
    {
        newRateRatio = m_cRateRatio / uNormal + m_raiRatio; // Algorithm Line 13
        newIncStage = 0;
    }
    else
    {
        newRateRatio = m_cRateRatio + m_raiRatio; // Algorithm Line 17
        newIncStage = m_incStage + 1;
    }
    // Update the rate ratio of the socket state
    SetRateRatio(newRateRatio);

    // Update current rate and incStage if needed
    if (updateCurrent)
    {
        m_cRateRatio = m_sockState->CheckRateRatio(newRateRatio);
        m_incStage = newIncStage;
        if (m_immediateMode)
        {
            std::cout << Simulator::Now().GetMicroSeconds() << "us, u: " << m_u
                      << " rate: " << m_cRateRatio << std::endl;
        }
    }
}

void
RoCEv2Hpcc::CopyIntHop(const IntHop* src, uint32_t nhop)
{
    for (uint32_t i = 0; i < nhop; i++)
    {
        m_hops[i] = src[i];
    }
}

void
RoCEv2Hpcc::UpdateExperimentState(uint32_t idx)
{
    if (idx >= m_experimentStates.size())
    {
        return;
    }
    SetRateRatio(m_experimentStates[idx].second);
    if (idx + 1 < m_experimentStates.size())
    {
        Simulator::Schedule(m_experimentStates[idx + 1].first - Simulator::Now(),
                            &RoCEv2Hpcc::UpdateExperimentState,
                            this,
                            idx + 1);
    }
}

void
RoCEv2Hpcc::SetUString(std::string uStr)
{
    if (uStr.back() == 'u')
    {
        m_targetUtil = std::stod(uStr.substr(0, uStr.size() - 1));
    }
    else
    {
        m_targetUtil = std::stod(uStr);
    }
}

std::string
RoCEv2Hpcc::GetName() const
{
    return "HPCC";
}

void
RoCEv2Hpcc::Init()
{
    HpccHeader hd;
    m_extraHeaderSize += hd.GetSerializedSize(); // Packet has extra HPCCHeader
    m_extraAckSize += hd.GetSerializedSize();    // ACK has extra HPCCHeader

    m_lastUpdateSeq = 0;
    m_incStage = 0;
    m_cRateRatio = 1.;
    m_u = 1.;

    RegisterCongestionType(GetTypeId());
}

std::shared_ptr<RoCEv2CongestionOps::Stats>
RoCEv2Hpcc::GetStats() const
{
    return m_stats;
}

RoCEv2Hpcc::Stats::Stats()
{
    NS_LOG_FUNCTION(this);
    BooleanValue bv;
    if (GlobalValue::GetValueByNameFailSafe("detailedSenderStats", bv))
        bDetailedSenderStats = bv.Get();
    else
        bDetailedSenderStats = false;
}

void
RoCEv2Hpcc::Stats::RecordPacketSend(uint32_t seq, Time sendTs)
{
    if (bDetailedSenderStats)
    {
        m_inflightPkts.push_back(std::make_pair(seq, sendTs));
    }
}

void
RoCEv2Hpcc::Stats::RecordPacketDelay(uint32_t seq)
{
    if (bDetailedSenderStats)
    {
        auto pkt = m_inflightPkts.front();
        m_inflightPkts.pop_front();
        if (pkt.first != seq - 1)
        {
            NS_LOG_ERROR("seq mismatch: " << pkt.first << " != " << seq);
        }
        Time delay = Simulator::Now() - pkt.second;
        vPacketDelay.push_back(std::make_tuple(pkt.second, Simulator::Now(), delay));
    }
}

void
RoCEv2Hpcc::Stats::RecordU(double u)
{
    if (bDetailedSenderStats)
    {
        vU.push_back(std::make_pair(Simulator::Now(), u));
    }
}
} // namespace ns3
