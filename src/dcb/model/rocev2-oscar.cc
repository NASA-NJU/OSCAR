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

#include "rocev2-oscar.h"

#include "rocev2-socket.h"

#include "ns3/global-value.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoCEv2Oscar");

NS_OBJECT_ENSURE_REGISTERED(RoCEv2Oscar);
NS_OBJECT_ENSURE_REGISTERED(OscarOwdHeader);
NS_OBJECT_ENSURE_REGISTERED(OscarBaseHeader);

TypeId
RoCEv2Oscar::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::RoCEv2Oscar")
            .SetParent<RoCEv2CongestionOps>()
            .AddConstructor<RoCEv2Oscar>()
            .SetGroupName("Dcb")
            .AddAttribute("TargetQueueLength",
                          "Oscar's target queue length, set in bytes and will be converted to time",
                          StringValue("75KB"),
                          MakeStringAccessor(&RoCEv2Oscar::SetTargetQueueLengthInBytes),
                          MakeStringChecker())
            .AddAttribute("BlsDuration",
                          "The duration of batched least squares, in ns.",
                          TimeValue(Time("5us")),
                          MakeTimeAccessor(&RoCEv2Oscar::m_blsDuration),
                          MakeTimeChecker())
            .AddAttribute("RateAIRatio",
                          "Oscar's RateAI ratio.",
                          DoubleValue(0.005),
                          MakeDoubleAccessor(&RoCEv2Oscar::m_raiRatio),
                          MakeDoubleChecker<double>())
            .AddAttribute("RateLinearStart",
                          "Oscar's linear start ratio.",
                          DoubleValue(1. / 4.),
                          MakeDoubleAccessor(&RoCEv2Oscar::m_linearStartRatio),
                          MakeDoubleChecker<double>())
            .AddAttribute("ThreePointBls",
                          "If enabled, the BLS calculation will only use three points.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2Oscar::m_threePointBls),
                          MakeBooleanChecker())
            .AddAttribute(
                "RttRecordStructure",
                "The structure of RTT record, Deque or Map. Map is for reordering network.",
                EnumValue(RttRecordStructure::DEQUE),
                MakeEnumAccessor(&RoCEv2Oscar::m_rttRecordStructure),
                MakeEnumChecker(RttRecordStructure::DEQUE, "Deque", RttRecordStructure::MAP, "Map"))
            .AddAttribute("SignalType",
                          "The signal type congestion control.",
                          EnumValue(SignalType::OWD),
                          MakeEnumAccessor(&RoCEv2Oscar::SetSignalType),
                          MakeEnumChecker(SignalType::OWD, "OWD", SignalType::RTT, "RTT"))
            .AddAttribute("WindowRateDecouple",
                          "Window rate decouple or not",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2Oscar::m_windowRateDecouple),
                          MakeBooleanChecker());
    return tid;
}

RoCEv2Oscar::RoCEv2Oscar()
    : RoCEv2CongestionOps(std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2Oscar::RoCEv2Oscar(Ptr<RoCEv2SocketState> sockState)
    : RoCEv2CongestionOps(sockState, std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2Oscar::~RoCEv2Oscar()
{
    NS_LOG_FUNCTION(this);
}

void
RoCEv2Oscar::SetReady()
{
    NS_LOG_FUNCTION(this);
    SetLimiting(true);
    // Call base class's SetRateRatio to set the start rate ratio
    // As the SetRateRatio of this class may has PRR to affect the cwnd
    // In the first RTT, do not inflate the cwnd
    RoCEv2CongestionOps::SetRateRatio(m_startRateRatio);

    m_bls = OscarBatchedEstimator(m_blsDuration, m_sockState->GetPacketSize());

    if (m_signalType == RTT)
    {
        m_targetThreshold =
            m_sockState->GetBaseRtt() + ConvertBytesToTime(m_targetQueueLengthInBytes);
        m_rttCorrection = Time(0);
    }
    else if (m_signalType == OWD)
    {
        m_targetThreshold =
            m_sockState->GetBaseOneWayDelay() + ConvertBytesToTime(m_targetQueueLengthInBytes);
        m_rttCorrection = m_sockState->GetBaseRtt() - m_sockState->GetBaseOneWayDelay();
    }

    m_rngDelayError = CreateObject<UniformRandomVariable>();
    m_rngDelayError->SetAttribute("Min", DoubleValue(0));
    m_rngDelayError->SetAttribute("Max", DoubleValue(1));
}

Time
RoCEv2Oscar::GetBase()
{
    if (m_signalType == RTT)
    {
        return m_sockState->GetBaseRtt();
    }
    else // OWD
    {
        return m_sockState->GetBaseOneWayDelay();
    }
}

void
RoCEv2Oscar::SetSignalType(enum SignalType signalType)
{
    NS_LOG_FUNCTION(this << signalType);
    m_signalType = signalType;

    OscarBaseHeader hbh = OscarBaseHeader(0);
    m_extraHeaderSize = hbh.GetSerializedSize();
    if (signalType == OWD)
    {
        // ACK carries an extra OscarOwdHeader to convey recvTs.
        OscarOwdHeader hoh;
        m_extraAckSize = hoh.GetSerializedSize();
    }
}

void
RoCEv2Oscar::UpdateStateSend(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);
    RoCEv2Header roceHeader;
    packet->RemoveHeader(roceHeader);
    OscarBaseHeader hbh = OscarBaseHeader((uint16_t)WindowRatio());
    packet->AddHeader(hbh);
    packet->AddHeader(roceHeader);
    if (m_rttRecordStructure == RttRecordStructure::DEQUE)
    {
        m_inflightPkts.push_back(std::make_tuple(roceHeader.GetPSN(),
                                                 Simulator::Now().GetNanoSeconds(),
                                                 m_sockState->GetRateRatioPercent(),
                                                 WindowRatio()));
    }
    else if (m_rttRecordStructure == RttRecordStructure::MAP)
    {
        m_inflightPktsMap[roceHeader.GetPSN()] = std::make_tuple(roceHeader.GetPSN(),
                                                                 Simulator::Now().GetNanoSeconds(),
                                                                 m_sockState->GetRateRatioPercent(),
                                                                 WindowRatio());
    }
    else
    {
        NS_FATAL_ERROR("Unknown RttRecordStructure");
    }
    m_inflightSize += m_sockState->GetPacketSize();
}

void
RoCEv2Oscar::UpdateStateWithGenACK(Ptr<Packet> packet, Ptr<Packet> ack)
{
    NS_LOG_FUNCTION(this << packet << ack);
    // Add an OscarOwdHeader into ack, which records the packet receive timestamp.
    // Note that the ACK has RoCEv2Header and AETHeader. Add OscarOwdHeader between
    // them.
    if (m_signalType == OWD)
    {
        RoCEv2Header roceHeader;
        OscarOwdHeader oscarHeader;
        ack->RemoveHeader(roceHeader);
        ack->AddHeader(oscarHeader);
        ack->AddHeader(roceHeader);
    }
}

void
RoCEv2Oscar::UpdateStateWithRcvACK(Ptr<Packet> ack,
                                             const RoCEv2Header& roce,
                                             const uint32_t senderNextPSN)
{
    NS_LOG_FUNCTION(this << ack << roce);

    uint32_t ackSeq = roce.GetPSN();
    bool rttPass = false;
    if (m_lastUpdateSeq != 0)
    {
        rttPass = ackSeq > m_lastUpdateSeq;
        if (rttPass)
        {
            m_lastUpdateSeq = senderNextPSN;
        }
    }
    else
    {
        m_lastUpdateSeq = senderNextPSN;
    }

    // Calculate the delay from the ACK
    OscarOwdHeader oscarHeader;
    ack->RemoveHeader(oscarHeader);

    PktInfo info;
    if (m_rttRecordStructure == RttRecordStructure::DEQUE)
    {
        info = m_inflightPkts.front();
        m_inflightPkts.pop_front();
    }
    else if (m_rttRecordStructure == RttRecordStructure::MAP)
    {
        info = m_inflightPktsMap[ackSeq - 1];
    }
    else
    {
        NS_FATAL_ERROR("Unknown RttRecordStructure");
    }
    m_inflightSize -= m_sockState->GetPacketSize();

    auto [sendPsn, sendTs, _1, _2] = info;
    NS_ASSERT_MSG(sendPsn == ackSeq - 1, "PSN not match when recv an ACK for Oscar");
    Time delay;
    if (m_signalType == RTT)
    {
        delay = Simulator::Now() - NanoSeconds(sendTs);
    }
    else if (m_signalType == OWD)
    {
        delay = oscarHeader.GetTs() - NanoSeconds(sendTs);
    }
    m_stats->RecordPacketDelay(NanoSeconds(sendTs), delay);

    bool blsCalculated = DoDelayGradientCalculation(info, delay);
    double delayGradient = 0;
    if (!m_threePointBls)
        delayGradient = m_bls.GetK();
    else
        delayGradient = m_bls.GetK3();

    double curRateRatio = m_sockState->GetRateRatioPercent();

    if (blsCalculated && m_bls.GetAvgDelay() < (GetBase() + Time("1us")).GetNanoSeconds())
    {
        curRateRatio += m_linearStartRatio;
        curRateRatio = std::min(1., curRateRatio);
    }
    else if (blsCalculated)
    {
        DoRateUpdate(m_bls.GetAverageRateRatio(),
                     m_bls.GetOracleAverageRateRatio(),
                     delayGradient,
                     m_bls.GetAvgDelay(),
                     curRateRatio);
    }

    if (curRateRatio != m_sockState->GetRateRatioPercent())
        SetRateRatio(curRateRatio);
}

bool
RoCEv2Oscar::DoDelayGradientCalculation(PktInfo ackedPkt, Time delay)
{
    Time nsSendtimeCorrected = NanoSeconds(std::get<1>(ackedPkt));

    m_tLastDelay = delay;
    m_tLastPktSendTime = NanoSeconds(std::get<1>(ackedPkt));

    Time tBlsWndBegin = NanoSeconds(m_bls.m_wndStart);

    bool blsCalculated = m_bls.Update((nsSendtimeCorrected).GetNanoSeconds(),
                                      delay.GetNanoSeconds(),
                                      Simulator::Now().GetNanoSeconds(),
                                      std::get<3>(ackedPkt));

    if (blsCalculated)
    {
        m_bls.Reset();
        m_stats->RecordPacketDelayGradientBls(nsSendtimeCorrected, m_bls.GetK());
        m_stats->RecordPacketDelayGradientBls3(nsSendtimeCorrected, m_bls.GetK3());

        m_stats->RecordOscarCompleteStats(
            Stats::OscarCompleteStats{Simulator::Now(),
                                        delay,
                                        tBlsWndBegin,
                                        NanoSeconds(m_bls.m_wndEnd),
                                        m_bls.GetK(),
                                        m_bls.GetAverageRateRatio(),
                                        m_bls.GetOracleAverageRateRatio(),
                                        -1}); // -1: rate ratio not yet known at this point
    }

    return blsCalculated;
}

void
RoCEv2Oscar::DoRateUpdate(double refenceCwnd,
                                    double refenceRate,
                                    double delayGradient,
                                    double delay,
                                    double& curRateRatio)
{
    // Clamp delay gradient above -0.9 to bound the (1 + delayGradient) divisor away from zero.
    delayGradient = std::max(delayGradient, -0.9);

    double scaleFactor = (double)(m_targetThreshold + m_rttCorrection).GetNanoSeconds() /
                         (m_bls.GetAvgDelay() + (double)m_rttCorrection.GetNanoSeconds());

    bool shouldHyper = m_bls.GetAvgDelay() + delayGradient * m_bls.m_duration.GetNanoSeconds() <
                       GetBase().GetNanoSeconds();

    if (m_windowRateDecouple)
    {
        double rateByDelayGradient = refenceRate / (1. + delayGradient);
        double rateByDelay = refenceCwnd * (scaleFactor);
        if (!shouldHyper)
        {
            m_windowRatio = rateByDelay;
        }
        curRateRatio = rateByDelayGradient;
    }
    else
    {
        double rateByDelayGradient = refenceRate / (1. + delayGradient);
        double rateByDelay = refenceCwnd * (scaleFactor);
        if (m_bls.GetAvgDelay() < (GetBase() + Time("1us")).GetNanoSeconds())
        {
            rateByDelay = refenceRate * (scaleFactor);
            rateByDelayGradient = rateByDelay;
        }
        if (refenceRate == 0 && delayGradient == 0)
        {
            rateByDelayGradient = rateByDelay;
        }
        Time delta = Time("0.5us");
        if ((m_bls.GetAvgDelay() + (double)m_rttCorrection.GetNanoSeconds()) <
            (double)(m_targetThreshold + m_rttCorrection - delta).GetNanoSeconds())
        {
            curRateRatio = std::max(rateByDelayGradient, rateByDelay);
        }
        else
        {
            curRateRatio = std::min(rateByDelayGradient, rateByDelay);
        }
    }

    curRateRatio += m_raiRatio;

    curRateRatio = std::min(1., curRateRatio);

    NS_ASSERT_MSG(curRateRatio > 0, "curRateRatio < 0");
}

double
RoCEv2Oscar::WindowRatio()
{
    return m_sockState->GetTxBuffer()->InflightPkts() /
           ((m_targetThreshold + m_rttCorrection).GetSeconds() *
            m_sockState->GetDeviceRate()->GetBitRate() / 8 / m_sockState->GetPacketSize());
}

void
RoCEv2Oscar::SetRateRatio(double rateRatio)
{
    m_sockState->SetRateRatioPercent(rateRatio);
    // After setting, rateRatio will be corrected to the range of [minRateRatio, 1]
    double curRateRatio = m_sockState->GetRateRatioPercent();

    uint64_t targetBdp = m_sockState->GetBaseBdp();
    if (targetBdp == 0)
    {
        return;
    }
    targetBdp += m_targetQueueLengthInBytes.GetValue();

    uint64_t targetCwnd = 0;
    if (m_windowRateDecouple)
    {
        targetCwnd = (uint64_t)(m_windowRatio * targetBdp);
    }
    else
    {
        targetCwnd = (uint64_t)(curRateRatio * targetBdp);
    }
    uint64_t cwnd = std::min(targetCwnd, m_sockState->GetBaseBdp());

    m_sockState->SetCwnd(cwnd);

    m_stats->RecordOscarCompleteStatsRateRatio(curRateRatio);
    m_stats->RecordCcRate(*(m_sockState->GetDeviceRate()) * m_sockState->GetRateRatioPercent());
}

void
RoCEv2Oscar::SetCwnd(uint32_t cwnd)
{
    m_sockState->SetCwnd(cwnd);

    double rateRatio = (double)cwnd / m_sockState->GetBaseBdp();
    m_sockState->SetRateRatioPercent(rateRatio);
}

void
RoCEv2Oscar::ResetRttRecord()
{
    m_lastUpdateSeq = m_sockState->GetTxBuffer()->GetFrontPsn();
}

std::string
RoCEv2Oscar::GetName() const
{
    return "Oscar";
}

void
RoCEv2Oscar::SetTargetQueueLengthInBytes(StringValue targetQueueLength)
{
    m_targetQueueLengthInBytes = QueueSize(targetQueueLength.Get());
}

Time
RoCEv2Oscar::ConvertBytesToTime(QueueSize bytes)
{
    return Time::FromDouble(bytes.GetValue() * 8. / m_sockState->GetDeviceRate()->GetBitRate(),
                            Time::S);
}

void
RoCEv2Oscar::Init()
{
    m_tLastDelay = Time(0);
    m_tLastPktSendTime = Time(0);
    m_lastUpdateSeq = 0;
    m_inflightSize = 0;

    RegisterCongestionType(GetTypeId());
}

std::shared_ptr<RoCEv2CongestionOps::Stats>
RoCEv2Oscar::GetStats() const
{
    return m_stats;
}

RoCEv2Oscar::Stats::Stats()
{
    NS_LOG_FUNCTION(this);
    BooleanValue bv;
    if (GlobalValue::GetValueByNameFailSafe("detailedSenderStats", bv))
        bDetailedSenderStats = bv.Get();
    else
        bDetailedSenderStats = false;
}

void
RoCEv2Oscar::Stats::RecordPacketDelay(Time sendTime, Time delay)
{
    if (bDetailedSenderStats)
    {
        vPacketDelay.push_back(std::make_tuple(sendTime, Simulator::Now(), delay));
    }
}

void
RoCEv2Oscar::Stats::RecordPacketDelayGradientBls(Time sendTime, double delayGradient)
{
    if (bDetailedSenderStats)
    {
        vPacketDelayGradientBls.push_back(
            std::make_tuple(sendTime, Simulator::Now(), delayGradient));
    }
}

void
RoCEv2Oscar::Stats::RecordPacketDelayGradientBls3(Time sendTime, double delayGradient)
{
    if (bDetailedSenderStats)
    {
        vPacketDelayGradientBls3.push_back(
            std::make_tuple(sendTime, Simulator::Now(), delayGradient));
    }
}

void
RoCEv2Oscar::Stats::RecordOscarCompleteStats(
    RoCEv2Oscar::Stats::OscarCompleteStats&& stats)
{
    if (bDetailedSenderStats)
    {
        vOscarCompleteStats.push_back(stats);
    }
}

void
RoCEv2Oscar::Stats::RecordOscarCompleteStatsRateRatio(double updatedRateRatio)
{
    if (bDetailedSenderStats)
    {
        // If the last record is not complete, complete it
        if (!vOscarCompleteStats.empty() && vOscarCompleteStats.back().updatedRateRatio == -1)
        {
            vOscarCompleteStats.back().updatedRateRatio = updatedRateRatio;
        }
    }
}

} // namespace ns3
