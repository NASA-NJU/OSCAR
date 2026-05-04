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

#include "rocev2-powertcp.h"

#include "rocev2-socket.h"

#include "ns3/global-value.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoCEv2Powertcp");

/********************
 * POWER TCP
 ********************/

NS_OBJECT_ENSURE_REGISTERED(RoCEv2Powertcp);

TypeId
RoCEv2Powertcp::GetTypeId()
{
    static TypeId tid = TypeId("ns3::RoCEv2Powertcp")
                            .SetParent<RoCEv2CongestionOps>()
                            .AddConstructor<RoCEv2Powertcp>()
                            .SetGroupName("Dcb")
                            .AddAttribute("Gamma",
                                          "Powertcp's EWMA parameter gamma",
                                          DoubleValue(0.9),
                                          MakeDoubleAccessor(&RoCEv2Powertcp::m_gamma),
                                          MakeDoubleChecker<double>())
                            .AddAttribute("RateAIRatio",
                                          "Powertcp's RateAI ratio. Act as beta in paper.",
                                          DoubleValue(0.0005),
                                          MakeDoubleAccessor(&RoCEv2Powertcp::m_raiRatio),
                                          MakeDoubleChecker<double>())
                            .AddAttribute("ArrivalRateBound",
                                          "Powertcp's arrival rate bound.",
                                          BooleanValue(true),
                                          MakeBooleanAccessor(&RoCEv2Powertcp::m_arrivalRateBound),
                                          MakeBooleanChecker())
                            .AddAttribute("DeltaTBound",
                                          "Powertcp's deltaT bound.",
                                          BooleanValue(true),
                                          MakeBooleanAccessor(&RoCEv2Powertcp::m_deltaTBound),
                                          MakeBooleanChecker())
                            .AddAttribute("GammaOld",
                                          "Powertcp's old gamma.",
                                          BooleanValue(true),
                                          MakeBooleanAccessor(&RoCEv2Powertcp::m_gammaOld),
                                          MakeBooleanChecker());
    return tid;
}

RoCEv2Powertcp::RoCEv2Powertcp()
    : RoCEv2CongestionOps(std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2Powertcp::RoCEv2Powertcp(Ptr<RoCEv2SocketState> sockState)
    : RoCEv2CongestionOps(sockState, std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2Powertcp::~RoCEv2Powertcp()
{
    NS_LOG_FUNCTION(this);
}

void
RoCEv2Powertcp::SetReady()
{
    NS_LOG_FUNCTION(this);
    // Reload any config before starting
    SetPacing(true);
    SetLimiting(true);
    SetCwnd(m_sockState->GetBaseBdp());

    m_beta = m_sockState->GetBaseBdp() * m_raiRatio; // unit: bytes
}

void
RoCEv2Powertcp::UpdateStateSend(Ptr<Packet> packet)
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
}

void
RoCEv2Powertcp::UpdateStateWithGenACK(Ptr<Packet> packet, Ptr<Packet> ack)
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
RoCEv2Powertcp::UpdateStateWithRcvACK(Ptr<Packet> ack,
                                      const RoCEv2Header& roce,
                                      const uint32_t senderNextPSN)
{
    NS_LOG_FUNCTION(this << ack << roce);
    // HPCC should remove the HPCC header of the ACK.
    uint32_t ackSeq = roce.GetPSN();
    m_canUpdate = ackSeq > m_lastUpdateSeq;

    if (m_lastUpdateSeq != 0)
    {
        // not first ACK
        NormPower(ack, ackSeq);
        UpdateWindow();
    }
    UpdateOld(
        ack,
        ackSeq,
        senderNextPSN); // if is first ACK, this function would update m_lastUpdateSeq and m_oldCwnd

    // Record the packet delay
    m_stats->RecordPacketDelay(ackSeq);
}

void
RoCEv2Powertcp::NormPower(Ptr<Packet> ack, const uint32_t ackSeq)
{
    Time baseRtt = m_sockState->GetBaseRtt();
    double powerNorm = 0;
    double deltaTNorm = 0;
    Time tau;
    HpccHeader hpccHeader;
    ack->PeekHeader(hpccHeader); // Remove later in RoCEv2Powertcp::UpdateOld
    const IntHop* inthop = hpccHeader.m_intHops;

    for (uint32_t i = 0; i < hpccHeader.m_nHop; i++)
    {
        /* Calculate power */
        Time dt = NanoSeconds(inthop[i].GetTimeDelta(m_hops[i])); // Algorithm Line 11
        double delayGradient = ((int64_t)inthop[i].GetQlen() - (int64_t)m_hops[i].GetQlen()) * 8.0 /
                               dt.GetSeconds(); // bps, Algorithm Line 12
        double txRate =
            inthop[i].GetBytesDelta(m_hops[i]) * 8.0 / dt.GetSeconds(); // bps, Algorithm Line 13
        double arrivalRate = txRate + delayGradient;                    // bps

        double bdp = inthop[i].GetLineRate().GetBitRate() * baseRtt.GetSeconds() /
                     8.0; // byte, Algorithm Line 15

        if (m_arrivalRateBound)
        {
            // Not in the paper, but according to git@github.com:inet-tub/ns3-datacenter.git
            arrivalRate = std::max(arrivalRate, inthop[i].GetLineRate().GetBitRate() * 0.5);
        }

        double power = (arrivalRate) * (inthop[i].GetQlen() +
                                        bdp); // Current(bps) * Voltage(byte), Algorithm Line 17
        /* Normalize power */
        double e = inthop[i].GetLineRate().GetBitRate() * bdp; // bps * byte, Algorithm Line 18
        double powerNormPrime = power / e;                     // Algorithm Line 19

        if (powerNormPrime > powerNorm) // Algorithm Line 20-22
        {
            powerNorm = powerNormPrime;
            deltaTNorm = dt.GetSeconds() / baseRtt.GetSeconds();
        }
    }

    // deltaTNorm should not be greater than 1.
    // Not in the paper, but according to git@github.com:inet-tub/ns3-datacenter.git
    if (m_deltaTBound)
    {
        deltaTNorm = std::min(deltaTNorm, 1.0);
    }
    m_power = m_power * (1.0 - deltaTNorm) + powerNorm * deltaTNorm; // Algorithm Line 24

    // Record power for statistics
    m_stats->RecordPower(m_power);
}

void
RoCEv2Powertcp::UpdateWindow()
{
    if (m_isTheta && !m_canUpdate)
    {
        // In Theta-PowerTCP, we should update cwnd per RTT.
        return;
    }
    double newPower =
        m_isTheta ? m_power : m_power / 0.95; // Not in the paper, but according to
                                              // git@github.com:inet-tub/ns3-datacenter.git

    // Use m_oldCwnd as the last term in gamma update
    // Not in the paper, but according to git@github.com:inet-tub/ns3-datacenter.git
    double newCwnd = 0;
    if (m_gammaOld)
        newCwnd = (static_cast<double>(m_oldCwnd) / newPower + m_beta) * m_gamma +
                  (m_oldCwnd) * (1.0 - m_gamma); // Bytes
    else
        newCwnd = (static_cast<double>(m_sockState->GetCwnd()) / newPower + m_beta) * m_gamma +
                  (m_sockState->GetCwnd()) * (1.0 - m_gamma); // Bytes
    double cwndPackets =
        ((static_cast<double>(m_sockState->GetCwnd()) + m_sockState->GetPacketSize() - 1.0) /
         m_sockState->GetPacketSize());
    if (cwndPackets < 1.0)
    {
        SetRateRatio(newCwnd / m_sockState->GetBaseBdp());
    }
    else
    {
        SetCwnd(static_cast<uint64_t>(newCwnd));
    }
}

void
RoCEv2Powertcp::UpdateOld(Ptr<Packet> ack, const uint32_t ackSeq, const uint32_t senderNextPSN)
{
    // Remove header and store the old INT
    HpccHeader hpccHeader;
    ack->RemoveHeader(hpccHeader);
    CopyIntHop(hpccHeader.m_intHops, hpccHeader.m_nHop);
    // Check if should update cwndOld
    if (m_canUpdate)
    {
        m_lastUpdateSeq = senderNextPSN;
        m_oldCwnd = m_sockState->GetCwnd();
    }
}

void
RoCEv2Powertcp::CopyIntHop(const IntHop* src, uint32_t nhop)
{
    for (uint32_t i = 0; i < nhop; i++)
    {
        m_hops[i] = src[i];
    }
}

std::string
RoCEv2Powertcp::GetName() const
{
    return "PowerTCP";
}

std::shared_ptr<RoCEv2CongestionOps::Stats>
RoCEv2Powertcp::GetStats() const
{
    return m_stats;
}

void
RoCEv2Powertcp::Init()
{
    HpccHeader hd;
    m_extraHeaderSize += hd.GetSerializedSize(); // Packet has extra HPCCHeader
    m_extraAckSize += hd.GetSerializedSize();    // ACK has extra HPCCHeader

    m_lastUpdateSeq = 0;
    m_power = 1.0;

    m_isTheta = false;

    RegisterCongestionType(GetTypeId());
}

RoCEv2Powertcp::Stats::Stats()
{
    NS_LOG_FUNCTION(this);
    BooleanValue bv;
    if (GlobalValue::GetValueByNameFailSafe("detailedSenderStats", bv))
        bDetailedSenderStats = bv.Get();
    else
        bDetailedSenderStats = false;
}

void
RoCEv2Powertcp::Stats::RecordPacketSend(uint32_t seq, Time sendTs)
{
    if (bDetailedSenderStats)
    {
        m_inflightPkts.push_back(std::make_pair(seq, sendTs));
    }
}

void
RoCEv2Powertcp::Stats::RecordPacketDelay(uint32_t seq)
{
    if (bDetailedSenderStats)
    {
        Time recvTs = Simulator::Now();
        // Find the send time for this sequence number
        for (auto it = m_inflightPkts.begin(); it != m_inflightPkts.end(); ++it)
        {
            if (it->first == seq)
            {
                Time sendTs = it->second;
                Time delay = recvTs - sendTs;
                vPacketDelay.emplace_back(sendTs, recvTs, delay);
                m_inflightPkts.erase(it);
                break;
            }
        }
    }
}

void
RoCEv2Powertcp::Stats::RecordPower(double powerNormPrime)
{
    if (bDetailedSenderStats)
    {
        vPower.emplace_back(Simulator::Now(), powerNormPrime);
    }
}

/********************
 * Theta-POWER TCP
 ********************/

NS_OBJECT_ENSURE_REGISTERED(RoCEv2ThetaPowertcp);

TypeId
RoCEv2ThetaPowertcp::GetTypeId()
{
    static TypeId tid = TypeId("ns3::RoCEv2ThetaPowertcp")
                            .SetParent<RoCEv2Powertcp>()
                            .AddConstructor<RoCEv2ThetaPowertcp>()
                            .SetGroupName("Dcb");
    return tid;
}

RoCEv2ThetaPowertcp::RoCEv2ThetaPowertcp()
    : RoCEv2Powertcp(),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2ThetaPowertcp::RoCEv2ThetaPowertcp(Ptr<RoCEv2SocketState> sockState)
    : RoCEv2Powertcp(sockState),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2ThetaPowertcp::~RoCEv2ThetaPowertcp()
{
    NS_LOG_FUNCTION(this);
}

void
RoCEv2ThetaPowertcp::UpdateStateSend(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);
    // Get packet's PSN from roceheader.
    RoCEv2Header roceHeader;
    packet->PeekHeader(roceHeader);
    // Add current time to the map.
    m_tsMap[roceHeader.GetPSN()] = Simulator::Now();
}

void
RoCEv2ThetaPowertcp::NormPower(Ptr<Packet> ack, const uint32_t ackSeq)
{
    Time rtt = Simulator::Now() - m_tsMap[ackSeq - 1];
    Time dt = Simulator::Now() - m_prevTc;

    double delayGradient = (rtt - m_prevRtt).GetSeconds() / dt.GetSeconds();
    double powerNorm = (delayGradient + 1) * rtt.GetNanoSeconds() * 1. /
                       m_sockState->GetBaseRtt().GetNanoSeconds();

    double deltaTNorm = dt.GetNanoSeconds() * 1. / m_sockState->GetBaseRtt().GetNanoSeconds();
    // deltaTNorm should not be greater than 1.
    // Not in the paper, but according to git@github.com:inet-tub/ns3-datacenter.git
    deltaTNorm = std::min(deltaTNorm, 1.0);
    m_power = m_power * (1.0 - deltaTNorm) + powerNorm * deltaTNorm;
}

void
RoCEv2ThetaPowertcp::UpdateOld(Ptr<Packet> ack, const uint32_t ackSeq, const uint32_t senderNextPSN)
{
    // Update tc and rtt
    m_prevRtt = Simulator::Now() - m_tsMap[ackSeq - 1];
    m_prevTc = Simulator::Now();
    // Check if should update cwndOld
    if (m_canUpdate)
    {
        m_lastUpdateSeq = senderNextPSN;
        m_oldCwnd = m_sockState->GetCwnd();
    }
}

void
RoCEv2ThetaPowertcp::Init()
{
    // Theta does not need HPCC header, but Powertcp::Init() does some initialization.
    // So we need to correct the size.
    HpccHeader hd;
    m_extraHeaderSize -= hd.GetSerializedSize();
    m_extraAckSize -= hd.GetSerializedSize();

    m_isTheta = true;

    m_stats = std::make_shared<Stats>();
    RegisterCongestionType(GetTypeId());
}

std::string
RoCEv2ThetaPowertcp::GetName() const
{
    return "Theta-PowerTCP";
}

} // namespace ns3
