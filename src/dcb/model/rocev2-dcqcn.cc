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

#include "rocev2-dcqcn.h"

#include "rocev2-socket.h"

#include "ns3/global-value.h"
#include "ns3/rocev2-header.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoCEv2Dcqcn");

NS_OBJECT_ENSURE_REGISTERED(RoCEv2Dcqcn);

TypeId
RoCEv2Dcqcn::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::RoCEv2Dcqcn")
            .SetParent<RoCEv2CongestionOps>()
            .AddConstructor<RoCEv2Dcqcn>()
            .SetGroupName("Dcb")
            .AddAttribute("Alpha",
                          "DCQCN's alpha (larger alpha means more aggressive rate reduction)",
                          DoubleValue(1.),
                          MakeDoubleAccessor(&RoCEv2Dcqcn::m_alpha),
                          MakeDoubleChecker<double>())
            .AddAttribute("G",
                          "DCQCN's g",
                          DoubleValue(1. / 16.),
                          MakeDoubleAccessor(&RoCEv2Dcqcn::m_g),
                          MakeDoubleChecker<double>())
            .AddAttribute("RateAIRatio",
                          "DCQCN's RateAI ratio",
                          DoubleValue(0.005),
                          MakeDoubleAccessor(&RoCEv2Dcqcn::m_raiRatio),
                          MakeDoubleChecker<double>())
            .AddAttribute("HraiRatio",
                          "DCQCN's hrai ratio",
                          DoubleValue(0.01),
                          MakeDoubleAccessor(&RoCEv2Dcqcn::m_hraiRatio),
                          MakeDoubleChecker<double>())
            .AddAttribute("BytesThreshold",
                          "DCQCN's BytesThreshold",
                          UintegerValue(10 * 1024 * 1024),
                          MakeUintegerAccessor(&RoCEv2Dcqcn::m_bytesThreshold),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("F",
                          "DCQCN's F",
                          UintegerValue(5),
                          MakeUintegerAccessor(&RoCEv2Dcqcn::m_F),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("AlphaTimerDelay",
                          "DCQCN's alpha timer delay",
                          TimeValue(MicroSeconds(55)),
                          MakeTimeAccessor(&RoCEv2Dcqcn::m_alphaTimerDelay),
                          MakeTimeChecker())
            .AddAttribute("RateTimerDelay",
                          "DCQCN's rate timer delay",
                          TimeValue(MicroSeconds(55)),
                          MakeTimeAccessor(&RoCEv2Dcqcn::m_rateTimerDelay),
                          MakeTimeChecker())
            .AddAttribute("StartTargetRateRatio",
                          "DCQCN's start target rate ratio",
                          DoubleValue(1),
                          MakeDoubleAccessor(&RoCEv2Dcqcn::m_targetRateRatio),
                          MakeDoubleChecker<double>());
    return tid;
}

RoCEv2Dcqcn::RoCEv2Dcqcn()
    : RoCEv2CongestionOps(std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2Dcqcn::RoCEv2Dcqcn(Ptr<RoCEv2SocketState> sockState)
    : RoCEv2CongestionOps(sockState,std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2Dcqcn::~RoCEv2Dcqcn()
{
    NS_LOG_FUNCTION(this);
}

void
RoCEv2Dcqcn::SetReady()
{
    NS_LOG_FUNCTION(this);
    SetRateRatio(m_startRateRatio);
    m_alphaTimer.SetDelay(m_alphaTimerDelay); // 55
    m_rateTimer.SetDelay(m_rateTimerDelay);   // 1500
    m_rateTimer.Schedule();
}

void
RoCEv2Dcqcn::UpdateStateSend(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);

    // Calculate the bytes without roceheader
    Ptr<Packet> copy = packet->Copy();
    RoCEv2Header roce;
    copy->RemoveHeader(roce);
    m_bytesCounter += copy->GetSize();
    if (m_bytesCounter >= m_bytesThreshold)
    {
        m_bytesCounter = 0;
        m_bytesUpdateIter++;
        UpdateRate();
    }
}

void
RoCEv2Dcqcn::UpdateStateWithCNP()
{
    NS_LOG_FUNCTION(this);

    double curRateRatio = m_sockState->GetRateRatioPercent();
    m_targetRateRatio = curRateRatio;
    SetRateRatio(curRateRatio * (1 - m_alpha / 2));

    m_alpha = (1 - m_g) * m_alpha + m_g;

    m_alphaTimer.Cancel(); // re-schedule timer
    if (!CheckStopCondition())
    {
        m_alphaTimer.Schedule();
        m_rateTimer.Cancel(); // re-schedule timer
        m_rateTimer.Schedule();
    }
    m_bytesCounter = 0;
    m_rateUpdateIter = 0;
    m_bytesUpdateIter = 0;
}

void
RoCEv2Dcqcn::UpdateAlpha()
{
    NS_LOG_FUNCTION(this);

    m_alpha *= 1 - m_g;
    if (!CheckStopCondition())
    {
        m_alphaTimer.Schedule();
    }
}

void
RoCEv2Dcqcn::RateTimerTriggered()
{
    NS_LOG_FUNCTION(this);

    m_rateUpdateIter++;
    UpdateRate();
    if (!CheckStopCondition())
    {
        m_rateTimer.Schedule();
    }
}

void
RoCEv2Dcqcn::UpdateRate()
{
    NS_LOG_FUNCTION(this);

    if (Simulator::GetContext() == 1)
    {
        NS_LOG_DEBUG("DCQCN: Update rate at time " << Simulator::Now().GetMicroSeconds() << "us");
    }

    double curRateRatio = m_sockState->GetRateRatioPercent();
    double old = curRateRatio;
    if (m_rateUpdateIter > m_F && m_bytesUpdateIter > m_F)
    { // Hyper increase
        // uint32_t i = std::min(m_rateUpdateIter, m_bytesUpdateIter) - m_F + 1;
        // m_targetRateRatio = std::min(m_targetRateRatio + i * m_hraiRatio, 1.);
        m_targetRateRatio = std::min(m_targetRateRatio + m_hraiRatio, 1.);
    }
    else if (m_rateUpdateIter > m_F || m_bytesUpdateIter > m_F)
    { // Additive increase
        m_targetRateRatio = std::min(m_targetRateRatio + m_raiRatio, 1.);
    }
    // else m_rateUpdateIter < m_F && m_bytesUpdateIter < m_F
    // Fast recovery: don't need to update target rate

    SetRateRatio((m_targetRateRatio + curRateRatio) / 2);
    if (old < 1.)
    {
        NS_LOG_DEBUG("DCQCN: Rate update from " << old * 100 << "% to " << curRateRatio * 100
                                                << "% at time "
                                                << Simulator::Now().GetMicroSeconds() << "us");
    }
}

void
RoCEv2Dcqcn::Init()
{
    // Init timer
    m_alphaTimer = Timer(Timer::CANCEL_ON_DESTROY);
    m_alphaTimer.SetFunction(&RoCEv2Dcqcn::UpdateAlpha, this);

    m_rateTimer.SetFunction(&RoCEv2Dcqcn::RateTimerTriggered, this);
    // Init others
    m_bytesCounter = 0;
    m_rateUpdateIter = 0;
    m_bytesUpdateIter = 0;
    // m_targetRateRatio = 1.;
    RegisterCongestionType(GetTypeId());
}

void
RoCEv2Dcqcn::SetRateAIRatio(double ratio)
{
    m_raiRatio = ratio * 1.;
}

void
RoCEv2Dcqcn::SetRateHyperAIRatio(double ratio)
{
    m_hraiRatio = ratio * 1.;
}

std::string
RoCEv2Dcqcn::GetName() const
{
    return "DCQCN";
}

} // namespace ns3
