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
 * Author: ChangLiu <liuchang_1307@163.com>
 */

#include "ppfc-pausable-queue-disc.h"

#include "dcb-traffic-control.h"
#include "fifo-queue-disc-ecn.h"
#include "ppfc-queue-disc-item.h"

#include "ns3/assert.h"
#include "ns3/boolean.h"
#include "ns3/fatal-error.h"
#include "ns3/global-value.h"
#include "ns3/integer.h"
#include "ns3/log-macros-enabled.h"
#include "ns3/log.h"
#include "ns3/object-base.h"
#include "ns3/object-factory.h"
#include "ns3/queue-disc.h"
#include "ns3/queue-item.h"
#include "ns3/queue-size.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/type-id.h"
#include "ns3/uinteger.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("PPfcPausableQueueDisc");

NS_OBJECT_ENSURE_REGISTERED(PPfcPausableQueueDisc);

TypeId
PPfcPausableQueueDisc::GetTypeId()
{
    static TypeId tid = TypeId("ns3::PPfcPausableQueueDisc")
                            .SetParent<PausableQueueDisc>()
                            .SetGroupName("Dcb")
                            .AddConstructor<PPfcPausableQueueDisc>();
    return tid;
}

PPfcPausableQueueDisc::PPfcPausableQueueDisc()
    : PausableQueueDisc(),
      m_stats(std::make_shared<Stats>(this)),
      m_portQNum(0)
{
    NS_LOG_FUNCTION(this);
}

PPfcPausableQueueDisc::PPfcPausableQueueDisc(uint32_t port)
    : PausableQueueDisc(port),
      m_stats(std::make_shared<Stats>(this)),
      m_portQNum(0)
{
    NS_LOG_FUNCTION(this);
}

PPfcPausableQueueDisc::PPfcPausableQueueDisc(Ptr<Node> node, uint32_t port)
    : PausableQueueDisc(node, port),
      m_stats(std::make_shared<Stats>(this)),
      m_portQNum(0)
{
    NS_LOG_FUNCTION(this);
}

PPfcPausableQueueDisc::~PPfcPausableQueueDisc()
{
    NS_LOG_FUNCTION(this);
}

Ptr<PausableQueueDiscClass>
PPfcPausableQueueDisc::GetQueueDiscClass(std::size_t i) const
{
    NS_LOG_FUNCTION(this);
    return DynamicCast<PausableQueueDiscClass>(QueueDisc::GetQueueDiscClass(i));
}

void
PPfcPausableQueueDisc::SetPortQNum(uint32_t num)
{
    NS_LOG_FUNCTION(this);
    m_portQNum = num;
}

uint32_t
PPfcPausableQueueDisc::GetPortQNum() const
{
    NS_LOG_FUNCTION(this);
    return m_portQNum;
}

// liuchangTODO: only used in rockv2socket to check send packet to qdisc
// will this influence the ppfc?
// QueueSize
// PPfcPausableQueueDisc::GetInnerQueueSize(uint8_t priority) const
// {
//     NS_LOG_FUNCTION(this);
//     return GetQueueDiscClass(priority)->GetQueueDisc()->GetCurrentSize();
// }

bool
PPfcPausableQueueDisc::DoEnqueue(Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);

    // TODO: Use Classify to call PacketFilter

    // Get priority from item.
    // the priority is the index of q here, we calculate it in traffic-control-layer.
    uint32_t qIdx;
    Ptr<PPfcQueueDiscItem> ppfcItem = DynamicCast<PPfcQueueDiscItem>(item);
    qIdx = ppfcItem->GetQidx();
    // NS_ASSERT_MSG(priority < 8, "Priority should be 0~7 but here we have " << priority);
    Ptr<PausableQueueDiscClass> qdiscClass = GetQueueDiscClass(qIdx);
    bool retval = qdiscClass->GetQueueDisc()->Enqueue(item);
    if (!retval)
    {
        NS_LOG_WARN("PPfcPausableQueueDisc: enqueue failed on node "
                    << Simulator::GetContext()
                    << ", queue size=" << qdiscClass->GetQueueDisc()->GetCurrentSize());
    }
    m_traceEnqueueWithId(item, GetNodeAndPortId(), qIdx);
    return retval;
}

Ptr<QueueDiscItem>
PPfcPausableQueueDisc::DoDequeue()
{
    NS_LOG_FUNCTION(this);
    Ptr<QueueDiscItem> item = 0;

    // The strict priority is implemented
    // The order is from high to low priority
    for (uint32_t i = GetNQueueDiscClasses(); i-- > 0;)
    {
        Ptr<PausableQueueDiscClass> qdclass = GetQueueDiscClass(i);
        if ((!m_fcEnabled || !qdclass->IsPaused()) &&
            (item = qdclass->GetQueueDisc()->Dequeue()) != nullptr)
        {
            NS_LOG_LOGIC("Popoed from priority " << i << ": " << item);

            // If the qdice is empty after dequeue, try to call the m_sendDataCb
            if (qdclass->GetQueueDisc()->GetNBytes() == 0)
            {
                // If we are at switch, the m_sendData Callback is null
                if (!m_sendDataCallback.IsNull())
                    // Note that the first device is LoopbackNetDevice, but this is not safe
                    m_sendDataCallback(m_portIndex, i);
            }

            if (!m_tcEgress.IsNull())
                m_tcEgress(m_portIndex, i, item->GetPacket());
            return item;
        }
    }
    NS_LOG_LOGIC("Queue empty");
    return item;
}

Ptr<const QueueDiscItem>
PPfcPausableQueueDisc::DoPeek()
{
    NS_LOG_FUNCTION(this);
    Ptr<const QueueDiscItem> item;

    for (uint32_t i = 0; i < GetNQueueDiscClasses(); i++)
    {
        Ptr<PausableQueueDiscClass> qdclass = GetQueueDiscClass(i);
        if ((!m_fcEnabled || !qdclass->IsPaused()) &&
            (item = qdclass->GetQueueDisc()->Dequeue()) != nullptr)
        {
            NS_LOG_LOGIC("Peeked from priority " << i << ": " << item);
            return item;
        }
    }

    NS_LOG_LOGIC("Queue empty");
    return item;
}

bool
PPfcPausableQueueDisc::CheckConfig(void)
{
    NS_LOG_FUNCTION(this);
    if (GetNInternalQueues() > 0)
    {
        NS_LOG_ERROR("PPfcPausableQueueDisc cannot have internal queues");
        return false;
    }
    // if (m_fcEnabled && GetQuota () != 1)
    //   {
    //     NS_LOG_ERROR ("Quota of PPfcPausableQueueDisc should be 1");
    //     return false;
    //   }

    // If no queue disc class is set
    if (GetNQueueDiscClasses() == 0)
    {
        NS_ASSERT_MSG(m_portQNum > 0, "NO PorQNum IS SET!");
        // create 8 fifo queue discs
        ObjectFactory factory;
        factory.SetTypeId("ns3::FifoQueueDiscEcn");
        // Each inner fifo queue's size is equal to the total queue size
        factory.Set("MaxSize", QueueSizeValue(m_queueSize));
        for (uint8_t i = 0; i < m_portQNum; i++)
        {
            Ptr<QueueDisc> qd = factory.Create<QueueDisc>();
            qd->Initialize();
            Ptr<PausableQueueDiscClass> c = CreateObject<PausableQueueDiscClass>();
            c->SetQueueDisc(qd);
            AddQueueDiscClass(c);
        }
    }
    return true;
}

void
PPfcPausableQueueDisc::InitializeParams(void)
{
    NS_LOG_FUNCTION(this);
}

std::shared_ptr<PPfcPausableQueueDisc::Stats>
PPfcPausableQueueDisc::GetStats() const
{
    m_stats->CollectAndCheck();
    return m_stats;
}

void
PPfcPausableQueueDisc::SetDetailedSwitchStats(bool bDetailedQlengthStats)
{
    m_stats->bDetailedQlengthStats = bDetailedQlengthStats;
    for (uint8_t i = 0; i < m_portQNum; i++) // liuchangTODO: add a value of fifoQ num.
    {
        Ptr<FifoQueueDiscEcn> qd =
            DynamicCast<FifoQueueDiscEcn>(GetQueueDiscClass(i)->GetQueueDisc());
        if (qd == nullptr)
        {
            // Here we assume the inner queue is FifoQueueDiscEcn
            NS_LOG_ERROR("PPfcPausableQueueDisc: cannot cast inner queue to FifoQueueDiscEcn");
            return;
        }
        qd->GetStatsWithoutCollect()->bDetailedQlengthStats = bDetailedQlengthStats;
    }
}

PPfcPausableQueueDisc::Stats::Stats(Ptr<PPfcPausableQueueDisc> qdisc)
    : m_qdisc(qdisc)
{
    // Retrieve the global config values
    BooleanValue bv;
    if (GlobalValue::GetValueByNameFailSafe("detailedSwitchStats", bv))
        bDetailedQlengthStats = bv.Get();
    else
        bDetailedQlengthStats = false;
}

void
PPfcPausableQueueDisc::Stats::RecordPauseResume(uint32_t prio, bool paused)
{
    if (bDetailedQlengthStats)
    {
        vPauseResumeTime.emplace_back(Simulator::Now(), prio, paused);
    }
}

void
PPfcPausableQueueDisc::Stats::CollectAndCheck()
{
    // Collect the statistics from each inner queue
    for (uint8_t i = 0; i < m_qdisc->GetPortQNum(); i++) // liuchangTODO: add a value of fifoQ num.
    {
        Ptr<FifoQueueDiscEcn> qd =
            DynamicCast<FifoQueueDiscEcn>(m_qdisc->GetQueueDiscClass(i)->GetQueueDisc());
        if (qd == nullptr)
        {
            // Here we assume the inner queue is FifoQueueDiscEcn
            NS_LOG_ERROR("PPfcPausableQueueDisc: cannot cast inner queue to FifoQueueDiscEcn");
            return;
        }
        vQueueStats.emplace_back(qd->GetStats());
    }
}

} // namespace ns3
