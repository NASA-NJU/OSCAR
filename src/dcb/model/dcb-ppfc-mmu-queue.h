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

#ifndef DCB_PPFC_MMU_QUEUE_H
#define DCB_PPFC_MMU_QUEUE_H

#include "dcb-flow-control-mmu-queue.h"

namespace ns3
{
class DcbPPfcMmuQueue : public DcbFlowControlMmuQueue
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId(void);

    /**
     * \brief Get the type ID for the instance
     * \return the instance TypeId
     */
    virtual TypeId GetInstanceTypeId(void) const override;

    DcbPPfcMmuQueue();
    DcbPPfcMmuQueue(uint32_t reserveSize);

    virtual ~DcbPPfcMmuQueue();

    virtual uint32_t GetQueueLength();
    /**
     * \brief Check that the ingress has enough space to store the current packet.
     * \return return ture in per-port-fc.
    */
    virtual bool CheckIngressAdmission(uint32_t packetSize) override;

    /**
     * \brief Check that the egress has enough space to store the current packet.
     *
    */
    virtual bool CheckEgressAdmission(uint32_t packetSize) override;

    virtual void IngressIncrement(uint32_t packetSize) override;

    virtual void EgressIncrement(uint32_t packetSize) override;

    virtual void IngressDecrement(uint32_t packetSize) override;

    virtual void EgressDecrement(uint32_t packetSize) override;
    /**
     * \brief Get the buffer size monopolized by this queue.
     */
    virtual inline uint32_t GetExclusiveBufferSize() override
    {
        return m_reservedSize;
    }

    /**
     * \brief Get the shared buffer used by this queue.
     */
    virtual inline uint32_t GetExclusiveSharedBufferUsed() override
    {
        return m_queueLength > m_reservedSize ? m_queueLength - m_reservedSize : 0;
    }

  private:
    uint32_t m_queueLength; // the queue length in Bytes
    uint32_t m_reservedSize; // in Bytes
};
} // namespace ns3

#endif // DCB_PPFC_MMU_QUEUE_H