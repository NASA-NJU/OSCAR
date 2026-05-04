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
#include "rocev2-poseidon.h"

#include "rocev2-socket.h"

#include "ns3/global-value.h"
#include "ns3/poseidon-header.h"
#include "ns3/simulator.h"

#include <cmath>

namespace ns3
{
NS_LOG_COMPONENT_DEFINE("RoCEv2Poseidon");

NS_OBJECT_ENSURE_REGISTERED(RoCEv2Poseidon);

TypeId
RoCEv2Poseidon::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::RoCEv2Poseidon")
            .SetParent<RoCEv2CongestionOps>()
            .AddConstructor<RoCEv2Poseidon>()
            .SetGroupName("Dcb")
            .AddAttribute("M",
                          "Poseidon's “step” when updating the rate (m in paper)",
                          DoubleValue(0.25),
                          MakeDoubleAccessor(&RoCEv2Poseidon::m_m),
                          MakeDoubleChecker<double>())
            .AddAttribute("K",
                          "Poseidon's minimum target delay in microseconds (k in paper)",
                          UintegerValue(2),
                          MakeUintegerAccessor(&RoCEv2Poseidon::m_k),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("P",
                          "Poseidon's tune parameter (p in paper)",
                          UintegerValue(40),
                          MakeUintegerAccessor(&RoCEv2Poseidon::m_p),
                          MakeUintegerChecker<uint32_t>());
    return tid;
}

RoCEv2Poseidon::RoCEv2Poseidon()
    : RoCEv2CongestionOps(std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2Poseidon::RoCEv2Poseidon(Ptr<RoCEv2SocketState> sockState)
    : RoCEv2CongestionOps(sockState, std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2Poseidon::~RoCEv2Poseidon()
{
    NS_LOG_FUNCTION(this);
}

void
RoCEv2Poseidon::SetReady()
{
    NS_LOG_FUNCTION(this);

    SetCwnd(m_sockState->GetBaseBdp());
    RoCEv2CongestionOps::SetReady(); // May update cwnd if setting start cwnd
    // Eq. 3 in Poseidon's paper implies that if min_rate == 0, T(rate) = k
    m_mptOperator = m_sockState->GetMinRateRatio() == 0
                        ? 0.
                        : m_p * 1. / log(m_maxRateRatio / m_sockState->GetMinRateRatio());
}

void
RoCEv2Poseidon::UpdateStateSend(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);

    // Rmove the roceheader
    RoCEv2Header roceHeader;
    packet->RemoveHeader(roceHeader);
    // Add current time to the map.
    m_tsMap[roceHeader.GetPSN()] = Simulator::Now();
    // Add an empty Poseidon header into packet
    PoseidonHeader poseidonHeader;
    packet->AddHeader(poseidonHeader);
    // Add the roceheader back
    packet->AddHeader(roceHeader);
}

void
RoCEv2Poseidon::UpdateStateWithGenACK(Ptr<Packet> packet, Ptr<Packet> ack)
{
    NS_LOG_FUNCTION(this << packet << ack);
    PoseidonHeader poseidonHeader;
    packet->RemoveHeader(poseidonHeader);
    // And then copy to the ACK.
    // Note that the ACK has RoCEv2Header and AETHeader. And we should add HPCC header between them.
    RoCEv2Header roceHeader;
    ack->RemoveHeader(roceHeader);
    ack->AddHeader(poseidonHeader);
    ack->AddHeader(roceHeader);
}

void
RoCEv2Poseidon::UpdateStateWithRcvACK(Ptr<Packet> ack,
                                      const RoCEv2Header& roce,
                                      const uint32_t senderNextPSN)
{
    PoseidonHeader poseidonHeader;
    ack->RemoveHeader(poseidonHeader);
    uint16_t mpd = poseidonHeader.m_mpd;

    Time delay = Simulator::Now() - m_tsMap[roce.GetPSN() - 1];
    double cwnd = m_sockState->GetCwnd();
    double curRateRatio =
        std::min(1.0, cwnd * 8.0 / delay.GetSeconds() / m_sockState->GetDeviceRate()->GetBitRate());
    double alpha = log(m_maxRateRatio / curRateRatio);
    double mpt = alpha * m_mptOperator + m_k; // Eq. 3 in Poseidon's paper

    m_stats->RecordMpt(mpt);

    double updateRatio =
        exp((1. * mpt - 1. * mpd) * alpha * m_m / m_p); // Eq. 4 in Poseidon's paper

    if (mpd <= mpt)
    {
        // MI
        uint64_t cwndPkt =
            (cwnd + m_sockState->GetPacketSize() - 1.0) / m_sockState->GetPacketSize();
        cwnd *= 1.0 + (updateRatio - 1.0) / cwndPkt;
    }
    else if (m_canDecrease)
    {
        // Do MD
        cwnd *= updateRatio;
        // Schedule a timer to set m_canDecrease to true.
        m_canDecrease = false;
        m_canDecreaseTimer.SetDelay(delay);
        m_canDecreaseTimer.Schedule();
    }
    cwnd = std::min(cwnd, m_sockState->GetBaseBdp() * 1.);

    SetCwnd(ceil(cwnd));
    // Record for statistics, do not affect the cwnd and rateRatio
    // m_stats->RecordCcRate(*(m_sockState->GetDeviceRate()) * (static_cast<double>(cwnd) * 8. /
    // delay.GetSeconds() / m_sockState->GetDeviceRate()->GetBitRate()));
    m_stats->RecordCcRate(*(m_sockState->GetDeviceRate()) *
                          (static_cast<double>(cwnd) * 8. / m_sockState->GetBaseRtt().GetSeconds() /
                           m_sockState->GetDeviceRate()->GetBitRate()));
}

std::string
RoCEv2Poseidon::GetName() const
{
    return "Poseidon";
}

void
RoCEv2Poseidon::Init()
{
    NS_LOG_FUNCTION(this);
    PoseidonHeader poseidonHeader;
    m_extraHeaderSize += poseidonHeader.GetSerializedSize();
    m_extraAckSize += poseidonHeader.GetSerializedSize();

    // Set Timer
    m_canDecreaseTimer = Timer();
    m_canDecreaseTimer.SetFunction(&RoCEv2Poseidon::SetCanDecrease, this);
    m_canDecrease = true;
    RegisterCongestionType(GetTypeId());
}

RoCEv2Poseidon::Stats::Stats()
{
    NS_LOG_FUNCTION(this);
    BooleanValue bv;
    if (GlobalValue::GetValueByNameFailSafe("detailedSenderStats", bv))
        bDetailedSenderStats = bv.Get();
    else
        bDetailedSenderStats = false;
}

void
RoCEv2Poseidon::Stats::RecordMpt(uint16_t mpt)
{
    if (bDetailedSenderStats)
    {
        vMpt.push_back(std::make_pair(Simulator::Now(), mpt));
    }
}

} // namespace ns3