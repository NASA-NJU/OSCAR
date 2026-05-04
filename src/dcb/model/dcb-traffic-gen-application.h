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

#ifndef DCB_TRAFFIC_GEN_APPLICATION_H
#define DCB_TRAFFIC_GEN_APPLICATION_H

#include "dcb-base-application.h"
#include "dcb-net-device.h"
#include "rocev2-socket.h"
#include "udp-based-socket.h"

#include "ns3/application.h"
#include "ns3/data-rate.h"
#include "ns3/dc-topology.h"
#include "ns3/flow-identifier.h"
#include "ns3/inet-socket-address.h"
#include "ns3/random-variable-stream.h"
#include "ns3/rocev2-header.h"
#include "ns3/seq-ts-size-header.h"
#include "ns3/traced-callback.h"

#include <map>
#include <set>
#include <string>

namespace ns3
{

class Socket;

class FileRequestMetadata : public Object
{
  public:
    static TypeId GetTypeId(void);

    FileRequestMetadata()
    {
    }

    ~FileRequestMetadata()
    {
    }

    std::vector<Time> m_fileRequestInterval; //!< The file request interval
    std::vector<std::set<uint32_t>>
        m_fileRequestIntervalNodes; //!< The file request nodes in each file request interval
    std::vector<uint32_t> m_fileRequestIntervalDest; //!< The file request destination in
                                                     //!< each file request interval
};

class RecoveryMetadata : public Object
{
  public:
    static TypeId GetTypeId(void);

    RecoveryMetadata()
    {
    }

    ~RecoveryMetadata()
    {
    }

    std::vector<Time> m_recoveryInterval; //!< The recovery interval
    std::vector<std::set<uint32_t>>
        m_recoveryIntervalNodes; //!< The recovery nodes in each recovery interval
};

class ParallelismMetadata : public Object
{
  public:
    static TypeId GetTypeId(void);

    ParallelismMetadata()
    {
        m_curPassflowFinished = 0;
        m_totalCurPass = 0;
    }

    ~ParallelismMetadata()
    {
    }

    class WorkloadLayer
    {
      public:
        enum CommType
        {
            ALLTOALL,
            ALLREDUCE,
            NONE
        };

        const static uint32_t GPUHZ = 3e8; //!< The GPU frequency

        Time m_fwd_computeTime;         //!< The forward compute time
        CommType m_fwd_communicateType; //!< The forward communicate type
        uint64_t m_fwd_communicateSize; //!< The forward communicate size
        Time m_ig_computeTime;          //!< The input gradient compute time
        CommType m_ig_communicateType;  //!< The input gradient communicate type
        uint64_t m_ig_communicateSize;  //!< The input gradient communicate size
        Time m_wg_computeTime;          //!< The weight gradient compute time
        CommType m_wg_communicateType;  //!< The weight gradient communicate type
        uint64_t m_wg_communicateSize;  //!< The weight gradient communicate size

        WorkloadLayer(uint32_t fwd_hz,
                      CommType fwd_type,
                      uint64_t fwd_size,
                      uint32_t ig_hz,
                      CommType ig_type,
                      uint64_t ig_size,
                      uint32_t wg_hz,
                      CommType wg_type,
                      uint64_t wg_size)
            : m_fwd_computeTime(NanoSeconds(fwd_hz * 1e9 / GPUHZ)),
              m_fwd_communicateType(fwd_type),
              m_fwd_communicateSize(fwd_size),
              m_ig_computeTime(NanoSeconds(ig_hz * 1e9 / GPUHZ)),
              m_ig_communicateType(ig_type),
              m_ig_communicateSize(ig_size),
              m_wg_computeTime(NanoSeconds(wg_hz * 1e9 / GPUHZ)),
              m_wg_communicateType(wg_type),
              m_wg_communicateSize(wg_size)
        {
        }
    };

    enum ParallelismType
    {
        RESNET = 0,
        VGG,
        MOE
    };

    // uint32_t m_passes;                           //!< The number of passes
    std::vector<uint32_t> m_parallelismNodes;    //!< The parallelism nodes
    std::vector<WorkloadLayer> m_workloadLayers; //!< The workload layers

    /***** static workloads *****/
    static inline std::vector<WorkloadLayer> ResNet = {{10944,
                                                        WorkloadLayer::CommType::NONE,
                                                        0,
                                                        10368,
                                                        WorkloadLayer::CommType::NONE,
                                                        0,
                                                        14144,
                                                        WorkloadLayer::CommType::ALLREDUCE,
                                                        147456},
                                                       {14156,
                                                        WorkloadLayer::CommType::NONE,
                                                        0,
                                                        12583,
                                                        WorkloadLayer::CommType::NONE,
                                                        0,
                                                        2952,
                                                        WorkloadLayer::CommType::ALLREDUCE,
                                                        2097153}};
    static inline std::vector<WorkloadLayer> Vgg = {{73143,
                                                     WorkloadLayer::CommType::NONE,
                                                     0,
                                                     73143,
                                                     WorkloadLayer::CommType::NONE,
                                                     0,
                                                     73143,
                                                     WorkloadLayer::CommType::ALLREDUCE,
                                                     4719616},
                                                    {18286,
                                                     WorkloadLayer::CommType::NONE,
                                                     0,
                                                     18286,
                                                     WorkloadLayer::CommType::NONE,
                                                     0,
                                                     18286,
                                                     WorkloadLayer::CommType::ALLREDUCE,
                                                     4719616}};

    static inline std::vector<WorkloadLayer> m_parallelismWorkloads[2] = {ResNet, Vgg};

    /***** Members for MOE *****/
    static inline std::vector<double> MoeProbability = {0.0020186727226848347,
                                                        0.0063083522583901085,
                                                        0.05677517032551098,
                                                        0.03305576583396417,
                                                        0.0035326772646984608,
                                                        0.07645722937168811,
                                                        0.17587686096391622,
                                                        0.012869038607115822,
                                                        0.22382033812768104,
                                                        0.0055513499873832955,
                                                        0.0035326772646984608,
                                                        0.0047943477163764825,
                                                        0.0070653545293969215,
                                                        0.15972747918243754,
                                                        0.20186727226848347,
                                                        0.026747413575574062};
    std::vector<double> MoeCdf;
    Ptr<UniformRandomVariable> m_moeRng; //!< The MOE random generator
    // XXX for MOE, now just support 16 nodes
    uint32_t SelectMoeNode();

    uint32_t m_curPassflowFinished; //!< Store the number of finished flows in the current pass
    uint32_t m_totalCurPass;        //!< Store the current pass number of all nodes
    const Time m_pollingInterval = MicroSeconds(1); //!< The polling interval
};

class ParallelismTag : public Tag
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(TagBuffer buf) const override;
    void Deserialize(TagBuffer buf) override;
    void Print(std::ostream& os) const override;
    void SetParallelismType(uint32_t type);
    uint32_t GetParallelismType() const;

  private:
    // ParallelismMetadata::WorkloadLayer::CommType m_commType; //!< The communication type
    uint32_t m_commType; //!< The communication type
    // TODO
};

class CoflowCdfMetadata : public Object
{
  public:
    static TypeId GetTypeId(void);

    CoflowCdfMetadata()
    {
    }

    ~CoflowCdfMetadata()
    {
    }

    /* members read from -num file*/
    std::vector<uint32_t> m_mrSize;                      //!< The map reduce size
    std::vector<std::pair<uint32_t, uint32_t>> m_mrNums; //!< The map reduce numbers, {map, reduce}

    std::vector<Time> m_coflowCdfInterval; //!< The coflow interval
    std::vector<std::set<uint32_t>>
        m_coflowCdfIntervalSrcNodes; //!< The coflow src nodes in each coflow interval
    std::vector<std::set<uint32_t>>
        m_coflowCdfIntervalDstNodes;               //!< The coflow dst nodes in each coflow interval
    std::vector<uint64_t> m_coflowCdfIntervalSize; //!< The coflow size in each coflow interval
    Ptr<EmpiricalRandomVariable> m_coflowCdfMapperNumRng;  //!< The mapper number random generator
    Ptr<EmpiricalRandomVariable> m_coflowCdfReducerNumRng; //!< The reducer number random generator

    void ReadingCoflowNums(std::string filename);
    void ReadingCoflowNumsCdf(std::string mapperFilename, std::string reducerFilename);
};

/**
 * \ingroup dcb
 * \brief A application that generates traffic and sends it to a destination.
 */
class DcbTrafficGenApplication : public DcbBaseApplication
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId(void);

    /**
     * \brief Create an application in topology node nodeIndex.
     * The application will randomly choose a node as destination and send flows.
     */
    // DcbTrafficGenApplication (Ptr<DcTopology> topology, uint32_t nodeIndex);

    /**
     * \brief Create an application in topology node nodeIndex destined to destIndex.
     * The application will send flows from nodeIndex to destIndex.
     * * If the destIndex is negative, the application will randomly choose a node as the
     * destination.
     */
    DcbTrafficGenApplication();
    DcbTrafficGenApplication(Ptr<DcTopology> topology, uint32_t nodeIndex);
    virtual ~DcbTrafficGenApplication();

    // The message size trace CDF in <size (in Byte), probility>
    typedef const std::vector<std::pair<uint32_t, double>> TraceCdf;

    bool m_isPartofCdf; //<! Whether the flow size is part of the CDF

    // uint32_t m_partofCdfStartSize; //<! The start size of the part of the CDF
    // uint32_t m_partofCdfEndSize;   //<! The end size of the part of the CDF

    enum PartofCdfType
    {
        SIZE,
        PROB,
        BOTH
    };

    PartofCdfType m_partofCdfType;     //<! The type of the part of the CDF
    uint32_t m_partofCdfPrio;          //<! The priority of the part of the CDF
    uint32_t m_partofCdfTotalPrioNums; //<! The total priority nums of the part of the CDF

    void SetFlowCdf(const TraceCdf& cdf);

    void SetFlowMeanArriveInterval(double interval);

    constexpr static inline const uint64_t MSS = 1000; // bytes

    enum TrafficPattern
    {
        CDF,
        SEND_ONCE,
        SEND_TO,
        FIXED_SIZE,
        RECOVERY,
        FILE_REQUEST,
        CHECKPOINT,
        PARALLELISM,
        COFLOW_CDF,
        RING,
        ALL2ALLV
    };

    virtual void FlowCompletes(Ptr<UdpBasedSocket> socket) override;

    class Stats : public DcbBaseApplication::Stats
    {
      public:
        // constructor
        Stats(Ptr<DcbTrafficGenApplication> app);
        Ptr<DcbTrafficGenApplication> m_app;

        virtual ~Stats()
        {
        } // Make the base class polymorphic

        bool isCollected; //<! Whether the stats is collected

        // Collect the statistics and check if the statistics is correct
        void CollectAndCheck(std::map<Ptr<Socket>, Flow*> flows);

        // No getter for simplicity
    };

    virtual std::shared_ptr<DcbBaseApplication::Stats> GetStats() const;

  private:
    /**
     * \brief Init fields, e.g., RNGs and m_socketLinkRate.
     */
    virtual void InitMembers() override;

    /**
     * \brief Schedule the next On period start
     */
    void ScheduleNextFlow(const Time& startTime);

    /**
     * \brief Get destination node index of one flow.
     * If m_destNode is negative, return a random destination.
     * Else return m_destNode.
     */
    uint32_t GetDestinationNode();

    /**
     * \brief Get next random flow start time.
     */
    Time GetNextFlowArriveInterval() const;

    /**
     * \brief Get next random flow size in bytes.
     */
    uint32_t GetNextFlowSize();

    // helpers
    /**
     * \brief Cancel all pending events.
     */
    // void CancelEvents ();

    /**
     * \brief Schedule a flow until the stop condition is satisfied.
     *
     * Now this function is only used for foreground flows.
     */
    void ScheduleAForegroundFlow();

    /**
     * \brief Generate traffic according to the traffic pattern.
     */
    virtual void GenerateTraffic() override;

    /**
     * \brief Calculate parameters of traffic.
     */
    virtual void CalcTrafficParameters() override;

    /**
     * \brief Handle a packet reception.
     *
     * This function is called by lower layers.
     *
     * \param socket the socket the packet was received to.
     */
    virtual void HandleRead(Ptr<Socket> socket) override;
    uint32_t GenDestNodeIdFromDestCdf();
    void ConstructAll2AllVDestCdf();

    std::shared_ptr<Stats> m_stats;

    Ptr<EmpiricalRandomVariable> m_flowSizeRng; //!< Flow size random generator
    bool m_isFixedFlowArrive;                   //!< Whether the flow arrive interval is fixed
    Time m_fixedFlowArriveInterval;             //!< Fixed flow arrive interval
    Ptr<ExponentialRandomVariable> m_flowArriveTimeRng; //!< Flow arrive time random generator
    Ptr<UniformRandomVariable> m_hostIndexRng;          //!< Host index random generator
    uint32_t m_destNode; //!< if not choosing random destination, store the destined node index here
    bool m_isFixedDest;  //!< Whether the destination is fixed
    bool m_interpolate;  //!< Whether the application interpolate the flow size

    /*********************************
     * Members for traffic patterns
     *********************************/
    enum TrafficPattern m_trafficPattern; //!< The traffic pattern of the application
    uint64_t m_trafficSize;               //!< The total size of the traffic
    double m_trafficLoad;                 //!< The traffic load

    std::string m_metadataGroupName; //!< The name of the metadata group

    /***** Members for send to *****/
    Time m_terminateTime; //!< The time to terminate sockets' sending of SendTo node
    void ScheduleTerminate();

    /***** Members for data recovery in storage cluster *****/
    double m_recoveryNodeRatio;               //!< The ratio of recovery traffic node in all nodes
    Ptr<RecoveryMetadata> m_recoveryMetadata; //!< The metadata of the recovery
    void CreateRecovery();

    /***** Members for file request cluster *****/
    uint32_t m_fileRequestNodesNum; //!< The number of file request nodes in the cluster
    uint32_t m_fileRequestIndex;    //!< The current file request index
    Ptr<FileRequestMetadata> m_fileRequestMetadata; //!< The metadata of the file request
    Time m_incastInterval;                          //!< The interval of the incast

    // Note that the file request mode is almost the same as the storage recovery mode, except that
    // every interval only one destination is chosen.
    void CreateFileRequest();

    /***** Members for checkpoint flows *****/
    /**
     * \brief Schedule the checkpoint flows to neighboring nodes.
     */
    void SchedulCheckpointFlow();

    /***** Members for coflow CDF *****/
    enum CoflowMRNumsType
    {
        BOUND,
        MRCDF
    };

    CoflowMRNumsType m_coflowCdfMRNumsType;     //!< The type of the coflow mr numbers
    std::string m_coflowCdfNumsFile;            //!< The file name of the coflow mr numbers
    std::string m_coflowCdfNumsMapperCdfFile;   //!< The file name of the coflow mr numbers
    std::string m_coflowCdfNumsReducerCdfFile;  //!< The file name of the coflow mr numbers

    uint32_t m_coflowCdfIndex;                  //!< The current coflow index
    Ptr<CoflowCdfMetadata> m_coflowCdfMetadata; //!< The metadata of the coflow CDF
    std::map<FlowIdentifier, uint32_t> m_coflowCdfIndexMap; //!< The index map of the coflow CDF

    void CreateCoflowCdf();
    void ScheduleNextCoflow(uint32_t coflowIndex);

    /***** Members for ring flows *****/
    uint32_t m_ringGroupNodesNum; //!< The number of nodes in one ring group
    uint32_t m_ringGroupIndex;    //!< The index of the current application's ring group
    uint32_t m_ringDestNode;      //!< The destination node of the current application

    static std::vector<std::vector<Time>> m_ringIntervals; //!< The ring intervals for each group
    static bool m_isRingInit;                              //!< Whether the ring is initialized
    void CreateRing();

    /***** Members for parallelism simulator *****/
    Ptr<ParallelismMetadata> m_parallelismMetadata; //!< The metadata of the parallelism
    void CreateParallelismFlow();
    uint32_t m_currentPass; //!< The current pass of the parallelism simulator
    uint32_t m_maxPass;     //!< The maximum pass, for now just used for MOE
    ParallelismMetadata::ParallelismType m_parallelismType;      //!< The parallelism type
    std::map<FlowIdentifier, uint32_t> m_parallelismFlowRcvSize; //!< The received size of each flow
    Time m_computeTime; //!< The compute time of the current node
    uint32_t
        m_parallelismIndexinMetaData; //!< The current index in the metadata's m_parallelismNodes

    void ScheduleNextParallelismFlow();

    void MoePollingCheckPass();

    /***** Members for continous foreground flows until background flows finished *****/
    enum TrafficType
    {
        FOREGROUND,
        BACKGROUND,
        NONE
    };

    TrafficType m_trafficType;                 //!< The type of the flow
    static std::vector<bool> m_bgFlowFinished; //!< Whether the background flows are finished

    void SetFlowType(std::string flowType);
    std::vector<double> m_destCdf;           //<! The destination CDF for all2allv
    Ptr<UniformRandomVariable> m_destCdfRng; //!< The destination random generator
    std::map<uint32_t, uint32_t> m_destCnter;  //<! The destination counter for all2allv
    uint32_t m_inferBatchSize;
}; // class DcbTrafficGenApplication

} // namespace ns3

#endif // DCB_TRAFFIC_GEN_APPLICATION_H
