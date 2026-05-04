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

#ifndef PPFC_PAUSABLE_QUEUE_DISC_H
#define PPFC_PAUSABLE_QUEUE_DISC_H

#include "fifo-queue-disc-ecn.h"
#include "pausable-queue-disc.h"

#include "ns3/node.h"
#include "ns3/queue-disc.h"
#include "ns3/queue-item.h"
#include "ns3/type-id.h"

namespace ns3
{

struct EcnConfig;

// class PPfcPausableQueueDiscClass : public QueueDiscClass
// {
//   public:
//     static TypeId GetTypeId();

//     PPfcPausableQueueDiscClass();
//     virtual ~PPfcPausableQueueDiscClass();

//     bool IsPaused() const;

//     void SetPaused(bool paused);

//   private:
//     bool m_isPaused;
// }; // PPfcPausableQueueDiscClass

class PPfcPausableQueueDisc : public PausableQueueDisc
{
  public:
    static TypeId GetTypeId(void);

    PPfcPausableQueueDisc();
    PPfcPausableQueueDisc(uint32_t port);
    PPfcPausableQueueDisc(Ptr<Node> node, uint32_t port);

    virtual ~PPfcPausableQueueDisc();

    /**
     * \brief Get the i-th queue disc class
     * \param i the index of the queue disc class
     * \return the i-th queue disc class.
     */
    Ptr<PausableQueueDiscClass> GetQueueDiscClass(std::size_t i) const;

    void SetPortQNum(uint32_t num);
    uint32_t GetPortQNum() const;

    class Stats
    {
      public:
        // constructor
        Stats(Ptr<PPfcPausableQueueDisc> qdisc);

        Ptr<PPfcPausableQueueDisc> m_qdisc;

        bool bDetailedQlengthStats;
        std::vector<std::tuple<Time, uint32_t, bool>>
            vPauseResumeTime; //<! Record the pause/resume time and the priority, true for paused,
                              // false for resumed

        // For now we only support FifoQueueDiscEcn as inner queue
        std::vector<std::shared_ptr<FifoQueueDiscEcn::Stats>>
            vQueueStats; //<! The queue statistics for each inner queue
        // Recorder function
        void RecordPauseResume(uint32_t prio, bool paused);

        // Collect the statistics and check if the statistics is correct
        void CollectAndCheck();

        // No getter for simplicity
    };

    std::shared_ptr<Stats> GetStats() const;

    /**
     * \brief Set the detailedSwitchStats for the queue disc and inner queue.
     *
     * This function is often used to disable the statistics collection for the queue disc and inner
     * queue. It will break the design of stats configuration, but the switch's stats is so heavy
     * that we have to do this. Sorry for that.
     */
    void SetDetailedSwitchStats(bool bDetailedQlengthStats);

  private:
    virtual bool DoEnqueue(Ptr<QueueDiscItem> item) override;

    virtual Ptr<QueueDiscItem> DoDequeue(void) override;

    virtual Ptr<const QueueDiscItem> DoPeek(void) override;

    virtual bool CheckConfig(void) override;

    virtual void InitializeParams(void) override;

    std::shared_ptr<Stats> m_stats;

    uint32_t m_portQNum; // should be set after build topo

}; // class PPfcPausableQueueDisc

} // namespace ns3

#endif // PPFC_PAUSABLE_QUEUE_DISC_H
