#ifndef OLSR_H_
#define OLSR_H_

/**
 * @file
 * @brief OLSR structs, constants, etc.
 * 
 * We're going to try really hard to mimic ns3 as much as possible in this
 * implementation.  We will assume the following:
 * - strictly symmetric links
 * - single interface
 */

#include <array>

// BOOST_POOL_NO_MT turns off multithreading
#define BOOST_POOL_NO_MT
#include <boost/pool/pool_alloc.hpp>

// From http://c-faq.com/misc/bitsets.html
#include <limits.h>		/* for CHAR_BIT */

#define BITMASK(b) (1 << ((b) % CHAR_BIT))
#define BITSLOT(b) ((b) / CHAR_BIT)
#define BITSET(a, b) ((a)[BITSLOT(b)] |= BITMASK(b))
#define BITCLEAR(a, b) ((a)[BITSLOT(b)] &= ~BITMASK(b))
#define BITTEST(a, b) ((a)[BITSLOT(b)] & BITMASK(b))
#define BITNSLOTS(nb) ((nb + CHAR_BIT - 1) / CHAR_BIT)

#include "ross.h"

extern FILE *olsr_event_log;

#define DEBUG 0

#define ENABLE_OPTIMISTIC 0

/** HELLO message interval */
#define HELLO_INTERVAL 2
/** TC message interval */
#define TC_INTERVAL 5
#define TOP_HOLD_TIME (3*TC_INTERVAL)
#define SA_INTERVAL 10
#define MASTER_SA_INTERVAL 60
#define OLSR_DUP_HOLD_TIME 30


#define OLSR_MPR_POWER 16     // dbm


/** max neighbors (for array implementation) */
#define OLSR_MAX_NEIGHBORS 16
#define OLSR_MAX_2_HOP (OLSR_MAX_NEIGHBORS * OLSR_MAX_NEIGHBORS)
#define OLSR_MAX_TOP_TUPLES (OLSR_MAX_NEIGHBORS * OLSR_MAX_NEIGHBORS)
#define OLSR_MAX_ROUTES (OLSR_MAX_NEIGHBORS * OLSR_MAX_NEIGHBORS)
#define OLSR_MAX_DUPES 64

/** For Situational Awareness (SA) */
#define MASTER_NODE ((s->get_local_address() / OLSR_MAX_NEIGHBORS) * OLSR_MAX_NEIGHBORS)
//#define MASTER_NODE ((s->local_address == 0) ? 0 : (OLSR_MAX_NEIGHBORS / s->local_address))

typedef tw_lpid o_addr; /**< We'll use this as a place holder for addresses */
typedef double Time;    /**< Use a double for time, check w/ Chris */
typedef enum {
    HELLO_RX,
    HELLO_TX,
    TC_RX,
    TC_TX,
    SA_RX,
    SA_TX,
    SA_MASTER_TX,
    SA_MASTER_RX,
    RWALK_CHANGE,
    OLSR_END_EVENT, // KEEP THIS LAST ELSE STATS ARRAY NOT BIG ENOUGH!!
} olsr_ev_type;

extern char const *event_names[];
//= {
//    "HELLO_RX",
//    "HELLO_TX",
//    "TC_RX",
//    "TC_TX",
//    "SA_RX",
//    "SA_TX",
//    "SA_MASTER_TX",
//    "SA_MASTER_RX",
//    "RWALK_CHANGE"
//};

/**
 struct hello - a basic hello message used by OLSR for link sensing / topology
 detection.
 
 Here is the ns3 hello class:
 @code
struct Hello
{
    struct LinkMessage {
        uint8_t linkCode;
        std::vector<Ipv4Address> neighborInterfaceAddresses;
    };
    
    uint8_t hTime;
    void SetHTime (Time time)
    {
        this->hTime = SecondsToEmf (time.GetSeconds ());
    }
    Time GetHTime () const
    {
        return Seconds (EmfToSeconds (this->hTime));
    }
    
    uint8_t willingness;
    std::vector<LinkMessage> linkMessages;
    
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize (void) const;
    void Serialize (Buffer::Iterator start) const;
    uint32_t Deserialize (Buffer::Iterator start, uint32_t messageSize);
};
 @endcode
 */

typedef struct /* Hello */
{
    /* No support for link codes yet! */
    char is_mpr[OLSR_MAX_NEIGHBORS];
    /** Addresses of our neighbors */
    o_addr neighbor_addrs[OLSR_MAX_NEIGHBORS];
    /** Number of neighbors, 0..n-1 */
    unsigned num_neighbors;
    
    /** HELLO emission interval */
    uint8_t hTime;
    
    /** Willingness to carry and foward traffic for other nodes */
    uint8_t Willingness;
    /* Link message size is an unnecessary field */
} hello;

/**
 struct TC - Topology Control information.
 
 The ns3 code is as follows:
 @code
struct Tc
{
    std::vector<Ipv4Address> neighborAddresses;
    uint16_t ansn;
    
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize (void) const;
    void Serialize (Buffer::Iterator start) const;
    uint32_t Deserialize (Buffer::Iterator start, uint32_t messageSize);
};
 @endcode
 */

typedef struct /* Tc */
{
    uint16_t ansn;
    o_addr neighborAddresses[OLSR_MAX_TOP_TUPLES];
    unsigned num_neighbors;
} TC;

typedef struct
{
    double lng;
    double lat;
} latlng;

typedef struct
{
    latlng ll[OLSR_MAX_NEIGHBORS];
} latlng_cluster;


typedef struct /* LinkTuple */
{
    /// Interface address of the local node.
    o_addr localIfaceAddr;
    /// Interface address of the neighbor node.
    o_addr neighborIfaceAddr;
    /// The link is considered bidirectional until this time.
    Time symTime;
    /// The link is considered unidirectional until this time.
    Time asymTime;
    /// Time at which this tuple expires and must be removed.
    Time time;
} link_tuple;

typedef struct /* NeighborTuple */
{
    /// Main address of a neighbor node.
    o_addr neighborMainAddr;
    /// Neighbor Type and Link Type at the four less significative digits.
    enum Status {
        STATUS_NOT_SYM = 0, // "not symmetric"
        STATUS_SYM = 1, // "symmetric"
    } status;
    /// A value between 0 and 7 specifying the node's willingness to carry traffic on behalf of other nodes.
    uint8_t willingness;
} neigh_tuple;

typedef struct /* TwoHopNeighborTuple */
{
    /// Main address of a neighbor.
    o_addr neighborMainAddr;
    /// Main address of a 2-hop neighbor with a symmetric link to nb_main_addr.
    o_addr twoHopNeighborAddr;
    /// Time at which this tuple expires and must be removed.
    Time expirationTime; // previously called 'time_'
} two_hop_neigh_tuple;

typedef struct /* MprSelectorTuple */
{
    /// Main address of a node which have selected this node as a MPR.
    o_addr mainAddr;
    /// Time at which this tuple expires and must be removed.
    // Time expirationTime; // previously called 'time_'
} mpr_sel_tuple;

typedef struct /* TopologyTuple */
{
    /// Main address of the destination.
    o_addr destAddr;
    /// Main address of a node which is a neighbor of the destination.
    o_addr lastAddr;
    /// Sequence number.
    uint16_t sequenceNumber;
    /// Time at which this tuple expires and must be removed.
    Time expirationTime;
} top_tuple;

/// An OLSR's routing table entry.
struct RT_entry /* RoutingTableEntry */
{
    o_addr destAddr; ///< Address of the destination node.
    o_addr nextAddr; ///< Address of the next hop.
    // Only one interface in our model
    //uint32_t interface; ///< Interface index
    uint32_t distance; ///< Distance in hops to the destination.
};

/// A Duplicate Tuple
struct dup_tuple/* DuplicateTuple */
{
    /// Originator address of the message.
    o_addr address;
    /// Message sequence number.
    uint16_t sequenceNumber;
    /// Indicates whether the message has been retransmitted or not.
    int retransmitted;
    /// List of interfaces which the message has been received on.
    // std::vector<Ipv4Address> ifaceList;
    /// Time at which this tuple expires and must be removed.
    Time expirationTime;
};

/**
 This struct contains all of the OLSR per-node state.  Not everything in the
 ns3 class is necessary or implemented, but here is the ns3 OlsrState class:
 @code
class OlsrState
{
    //  friend class Olsr;
    
protected:
    LinkSet m_linkSet;    ///< Link Set (RFC 3626, section 4.2.1).
    NeighborSet m_neighborSet;            ///< Neighbor Set (RFC 3626, section 4.3.1).
    TwoHopNeighborSet m_twoHopNeighborSet;        ///< 2-hop Neighbor Set (RFC 3626, section 4.3.2).
    TopologySet m_topologySet;    ///< Topology Set (RFC 3626, section 4.4).
    MprSet m_mprSet;      ///< MPR Set (RFC 3626, section 4.3.3).
    MprSelectorSet m_mprSelectorSet;      ///< MPR Selector Set (RFC 3626, section 4.3.4).
    DuplicateSet m_duplicateSet;  ///< Duplicate Set (RFC 3626, section 3.4).
    IfaceAssocSet m_ifaceAssocSet;        ///< Interface Association Set (RFC 3626, section 4.1).
    AssociationSet m_associationSet; ///<	Association Set (RFC 3626, section12.2). Associations obtained from HNA messages generated by other nodes.
    Associations m_associations;  ///< The node's local Host Network Associations that will be advertised using HNA messages.
};
 @endcode
 */

extern boost::fast_pool_allocator<uint16_t> uint16_allocator;
extern boost::fast_pool_allocator<o_addr> o_addr_allocator;
extern boost::fast_pool_allocator<unsigned> unsigned_allocator;
extern boost::fast_pool_allocator<double> double_allocator;
extern boost::fast_pool_allocator<RT_entry> RT_entry_allocator;
extern boost::fast_pool_allocator<dup_tuple> dup_tuple_allocator;
extern boost::fast_pool_allocator<neigh_tuple> neigh_tuple_allocator;
extern boost::fast_pool_allocator<two_hop_neigh_tuple> two_hop_neigh_tuple_allocator;
extern boost::fast_pool_allocator<mpr_sel_tuple> mpr_sel_tuple_allocator;
extern boost::fast_pool_allocator<top_tuple> top_tuple_allocator;

class node_state : public LP_State /*OlsrState */
{
private:
    /// Longitude for this node only
    std::shared_ptr<double> lng;
    /// Latitude for this node only
    std::shared_ptr<double> lat;

    /// this node's address
    std::shared_ptr<o_addr> local_address;

    // vector<NeighborTuple>
    std::shared_ptr<unsigned> num_neigh;
    std::array<std::shared_ptr<neigh_tuple>, OLSR_MAX_NEIGHBORS> neighSet;

    // vector<TwoHopNeighborTuple>
    std::shared_ptr<unsigned> num_two_hop;
    std::array<std::shared_ptr<two_hop_neigh_tuple>, OLSR_MAX_2_HOP> twoHopSet;

    // set<Ipv4Address>
    std::shared_ptr<unsigned> num_mpr;
    std::array<std::shared_ptr<o_addr>, OLSR_MAX_NEIGHBORS> mprSet;

    // vector<MprSelectorTuple>
    std::shared_ptr<unsigned> num_mpr_sel;
    std::array<std::shared_ptr<mpr_sel_tuple>, OLSR_MAX_NEIGHBORS> mprSelSet;

    // vector<TopologyTuple>
    std::shared_ptr<unsigned> num_top_set;
    std::array<std::shared_ptr<top_tuple>, OLSR_MAX_TOP_TUPLES> topSet;

    // vector<RoutingTableEntry>
    std::shared_ptr<unsigned> num_routes;
    std::array<std::shared_ptr<RT_entry>, OLSR_MAX_ROUTES> route_table;

    // vector<DuplicateTuple>
    std::shared_ptr<unsigned> num_dupes;
    std::array<std::shared_ptr<dup_tuple>, OLSR_MAX_DUPES> dupSet;

    std::shared_ptr<uint16_t> ansn {new uint16_t(0)};

public:
    unsigned Dy(o_addr target) const;
    RT_entry * Lookup(o_addr dest) const;
    neigh_tuple * FindSymNeighborTuple(o_addr mainAddr) const;
    top_tuple * FindTopologyTuple(o_addr destAddr, o_addr lastAddr) const;

    double get_lng() const { return *lng.get(); }
    void set_lng(double l) {
        lng = std::allocate_shared<double>(double_allocator, l);
    }

    double get_lat() const { return *lat.get(); }
    void set_lat(double l) {
        lat = std::allocate_shared<double>(double_allocator, l);
    }

    o_addr get_local_address() const { return *local_address.get(); }
    void set_local_address(o_addr l) {
        local_address = std::allocate_shared<o_addr>(o_addr_allocator, l);
    }

    unsigned get_num_neigh() const { return *num_neigh.get(); }
    void set_num_neigh(unsigned l) {
        num_neigh = std::allocate_shared<unsigned>(unsigned_allocator, l);
    }

    neigh_tuple* get_neighSet(unsigned l) const { return neighSet[l].get(); }
    void set_neighSet(unsigned idx, const neigh_tuple &nt)
    {
        neighSet[idx] = std::allocate_shared<neigh_tuple>(neigh_tuple_allocator, nt);
    }

    unsigned get_num_two_hop() const { return *num_two_hop.get(); }
    void set_num_two_hop(unsigned l) {
        num_two_hop = std::allocate_shared<unsigned>(unsigned_allocator, l);
    }

    two_hop_neigh_tuple* get_twoHopSet(unsigned l) const { return twoHopSet[l].get(); }
    void set_twoHopSet(unsigned idx, const two_hop_neigh_tuple &nt)
    {
        twoHopSet[idx] = std::allocate_shared<two_hop_neigh_tuple>(two_hop_neigh_tuple_allocator, nt);
    }

    unsigned get_num_mpr() const { return *num_mpr.get(); }
    void set_num_mpr(unsigned l) {
        num_mpr = std::allocate_shared<unsigned>(unsigned_allocator, l);
    }

    o_addr get_MprSet(unsigned l) const { return *mprSet[l].get(); }
    void set_MprSet(unsigned idx, o_addr nt)
    {
        mprSet[idx] = std::allocate_shared<o_addr>(o_addr_allocator, nt);
    }

    unsigned get_num_mpr_sel() const { return *num_mpr_sel.get(); }
    void set_num_mpr_sel(unsigned l) {
        num_mpr_sel = std::allocate_shared<unsigned>(unsigned_allocator, l);
    }

    mpr_sel_tuple get_MprSelSet(unsigned l) const { return *mprSelSet[l].get(); }
    void set_MprSelSet(unsigned idx, const mpr_sel_tuple &nt)
    {
        mprSelSet[idx] = std::allocate_shared<mpr_sel_tuple>(mpr_sel_tuple_allocator, nt);
    }

    unsigned get_num_top_set() const { return *num_top_set.get(); }
    void set_num_top_set(unsigned l) {
        num_top_set = std::allocate_shared<unsigned>(unsigned_allocator, l);
    }

    top_tuple* get_topSet(unsigned l) const { return topSet[l].get(); }
    std::shared_ptr<top_tuple>& get_topSetSP(unsigned l) { return topSet[l]; }
    void set_topSet(unsigned idx, const top_tuple &nt)
    {
        topSet[idx] = std::allocate_shared<top_tuple>(top_tuple_allocator, nt);
    }

    unsigned get_num_routes() const { return *num_routes.get(); }
    void set_num_routes(unsigned l) {
        num_routes = std::allocate_shared<unsigned>(unsigned_allocator, l);
    }

    RT_entry* get_route_table(unsigned l) const { return route_table[l].get(); }
    void set_route_table(unsigned idx, const RT_entry &nt)
    {
        route_table[idx] = std::allocate_shared<RT_entry>(RT_entry_allocator, nt);
    }

    unsigned get_num_dupes() const { return *num_dupes.get(); }
    void set_num_dupes(unsigned l) {
        num_dupes = std::allocate_shared<unsigned>(unsigned_allocator, l);
    }

    dup_tuple* get_dupSet(unsigned l) const { return dupSet[l].get(); }
    std::shared_ptr<dup_tuple>& get_dupSetSP(unsigned l) { return dupSet[l]; }
    void set_dupSet(unsigned idx, const dup_tuple &nt)
    {
        dupSet[idx] = std::allocate_shared<dup_tuple>(dup_tuple_allocator, nt);
    }

    uint16_t get_ansn() const { return *ansn.get(); }
    void set_ansn(uint16_t l) {
        ansn = std::allocate_shared<uint16_t>(uint16_allocator, l);
    }

    // vector<LinkTuple>
    //link_tuple linkSet[OLSR_MAX_NEIGHBORS];
    //unsigned num_tuples;









    
    // Not part of the state in ns3 but fits here mostly

    int SA_per_node[OLSR_MAX_NEIGHBORS];

    node_state() = default;
    node_state(const node_state &a) = default;

    node_state(node_state&& rhs) = default;              // move constructor
    node_state& operator=(node_state&& rhs) = default;   // move assignment operator

    node_state * clone() const override
    {
        return new node_state(*this);
    }
    
};

union message_type {
    hello h;
    TC t;
    latlng l;
    latlng_cluster llc;
};

typedef struct
{
    olsr_ev_type type;     ///< What type of message is this?
    uint8_t ttl;           ///< The Time To Live field for this packet
    o_addr originator;     ///< Node responsible for this event
    o_addr sender;         ///< Node to last touch this message (TC) or MITM (SA)
    o_addr destination;    ///< Destination node
    double lng;            ///< Longitude for 'sender' (above)
    double lat;            ///< Latitude for 'sender' (above)
    union message_type mt; ///< Union for message type
    unsigned long target;  ///< Target index into g_tw_lp
    uint16_t seq_num;      ///< Sequence number for this message
    int level;             ///< Level for SA_MASTER messages
#if ENABLE_OPTIMISTIC
    node_state state_copy;  ///< copy state for the lp that processes the event
#endif 
} olsr_msg_data;

void olsr_custom_mapping(void);
tw_lp * olsr_mapping_to_lp(tw_lpid lpid);

extern unsigned int nlp_per_pe;
extern char g_olsr_mobility;
extern unsigned int SA_range_start;
extern unsigned long long g_olsr_event_stats[OLSR_END_EVENT];
extern unsigned long long g_olsr_root_event_stats[OLSR_END_EVENT];
extern tw_lptype olsr_lps[];

#endif /* OLSR_H_ */
