#include "ross.h"
#include "olsr.h"
#include <assert.h>

/**
 * @file
 * @brief OLSR Driver
 *
 * Simple driver to test out various functionalities in the OLSR impl.
 */

boost::fast_pool_allocator<uint16_t> uint16_allocator;
boost::fast_pool_allocator<o_addr> o_addr_allocator;
boost::fast_pool_allocator<unsigned> unsigned_allocator;
boost::fast_pool_allocator<double> double_allocator;
boost::fast_pool_allocator<RT_entry> RT_entry_allocator;
boost::fast_pool_allocator<dup_tuple> dup_tuple_allocator;
boost::fast_pool_allocator<neigh_tuple> neigh_tuple_allocator;
boost::fast_pool_allocator<two_hop_neigh_tuple> two_hop_neigh_tuple_allocator;
boost::fast_pool_allocator<mpr_sel_tuple> mpr_sel_tuple_allocator;
boost::fast_pool_allocator<top_tuple> top_tuple_allocator;

double g_X[OLSR_MAX_NEIGHBORS];
double g_Y[OLSR_MAX_NEIGHBORS];

#define GRID_MAX 100
#define STAGGER_MAX 10
#define HELLO_DELTA 0.0001
#define OLSR_NO_FINAL_OUTPUT 1
#define USE_RADIO_DISTANCE 1
#define RWALK_INTERVAL 20

unsigned int nlp_per_pe = OLSR_MAX_NEIGHBORS;

// Used as scratch space for MPR calculations
unsigned g_Dy[OLSR_MAX_NEIGHBORS];
unsigned g_num_one_hop;
neigh_tuple g_mpr_one_hop[OLSR_MAX_NEIGHBORS];
unsigned g_num_two_hop;
two_hop_neigh_tuple g_mpr_two_hop[OLSR_MAX_2_HOP];
unsigned g_reachability[OLSR_MAX_NEIGHBORS];
neigh_tuple g_mpr_neigh_to_add;
unsigned g_mpr_num_add_nodes;
char g_covered[BITNSLOTS(OLSR_MAX_NEIGHBORS)];
char g_olsr_mobility = 'N';

unsigned long long g_olsr_event_stats[OLSR_END_EVENT];
unsigned long long g_olsr_root_event_stats[OLSR_END_EVENT];

char const *event_names[OLSR_END_EVENT] = {
    "HELLO_RX",
    "HELLO_TX",
    "TC_RX",
    "TC_TX",
    "SA_RX",
    "SA_TX",
    "SA_MASTER_TX",
    "SA_MASTER_RX",
    "RWALK_CHANGE"
};

FILE *olsr_event_log=nullptr;

unsigned region(o_addr a)
{
    return a / OLSR_MAX_NEIGHBORS;
}

unsigned int SA_range_start;

/**
 * Returns the lpid of the master SA aggregator for the region containing lpid.
 * For example, if OMN = 16 then we have 16 OLSR nodes followed by one master
 * on each pe.
 */
o_addr sa_master_for_level(o_addr lpid)
{
    // Get the region number
    int rnum = region(lpid);
    // Now correct for all the LPs before this aggregator
    rnum += SA_range_start * tw_nnodes();
    //printf("We're sending SA data to node %d\n", rnum);
    return rnum;
}

/**
 */
o_addr master_hierarchy(o_addr lpid, int level)
{
    long val;

    //printf("master_hiearchy(%lld,%d): ", lpid, level);

    val = (long)pow(2.0, (double)level);
    //printf("(val=%ld) ", val);

    // First, normalize the lpid
    lpid -= SA_range_start * tw_nnodes();
    assert(lpid >= 0);

    //printf("%lld -> ", lpid);

    lpid /= val;
    lpid *= val;

    //printf("%lld -> ", lpid);

    lpid += SA_range_start * tw_nnodes();

    //printf("%lld\n", lpid);

    return lpid;
}

/**
 * Initializer for OLSR
 */
void olsr_init(node_state *s, tw_lp *lp)
{
    hello *h;
    TC *t;
    tw_event *e;
    olsr_msg_data *msg;
    tw_stime ts;
    int i;

#if DEBUG
    fprintf( olsr_event_log, "OLSR Init LP %llu RNG Seeds Are: ", lp->gid);
    rng_write_state( lp->rng, olsr_event_log );
#endif

    //s->num_tuples = 0;
    s->set_num_neigh(0);
    s->set_num_two_hop(0);
    s->set_num_mpr(0);
    s->set_num_mpr_sel(0);
    s->set_num_top_set(0);
    s->set_num_dupes(0);
    for (i = 0; i < OLSR_MAX_NEIGHBORS; i++) {
        s->SA_per_node[i] = 0;
    }
    // Now we store the GID as opposed to an int from 0-OMN
    s->set_local_address(lp->gid);// % OLSR_MAX_NEIGHBORS;
    s->set_lng(tw_rand_unif(&lp->cur_state->rng) * GRID_MAX);
    s->set_lat(tw_rand_unif(&lp->cur_state->rng) * GRID_MAX);
    // printf("Initializing node %lu on CPU %llu\n", lp->gid, lp->pe->id);

    //g_X[s->local_address] = s->lng;
    //g_Y[s->local_address] = s->lat;
    // Build our initial HELLO_TX messages
    ts = g_tw_lookahead + tw_rand_unif(&lp->cur_state->rng) * STAGGER_MAX;
    e = tw_event_new(lp->gid, ts, lp);
    msg = (olsr_msg_data*)tw_event_data(e);
    msg->type = HELLO_TX;
    msg->originator = s->get_local_address();
    msg->lng = s->get_lng();
    msg->lat = s->get_lat();
    h = &msg->mt.h;
    h->num_neighbors = 0;
    tw_event_send(e);

    // Build our initial TC_TX messages
    ts = g_tw_lookahead + tw_rand_unif(&lp->cur_state->rng) * STAGGER_MAX;
    e = tw_event_new(lp->gid, ts, lp);
    msg = (olsr_msg_data*)tw_event_data(e);
    msg->type = TC_TX;
    msg->originator = s->get_local_address();
    msg->lng = s->get_lng();
    msg->lat = s->get_lat();
    t = &msg->mt.t;
    //t->num_mpr_sel = 0;
    t->num_neighbors = 0;
    tw_event_send(e);

    // Build our initial SA_TX messages
    ts = g_tw_lookahead + tw_rand_unif(&lp->cur_state->rng) * STAGGER_MAX + SA_INTERVAL;
    e = tw_event_new(lp->gid, ts, lp);
    msg = (olsr_msg_data*)tw_event_data(e);
    msg->type = SA_TX;
    msg->originator = s->get_local_address();
    msg->destination = MASTER_NODE;
    msg->lng = s->get_lng();
    msg->lat = s->get_lat();
    tw_event_send(e);

    if (g_olsr_mobility != 'n' && g_olsr_mobility != 'N') {
        // Build our initial RWALK_CHANGE messages
        ts = g_tw_lookahead + tw_rand_unif(&lp->cur_state->rng) * RWALK_INTERVAL + 1.0;
        e = tw_event_new(lp->gid, ts, lp);
        msg = (olsr_msg_data*)tw_event_data(e);
        msg->type = RWALK_CHANGE;
        msg->lng = tw_rand_unif(&lp->cur_state->rng) * GRID_MAX;
        msg->lat = tw_rand_unif(&lp->cur_state->rng) * GRID_MAX;
        tw_event_send(e);
    }

#if 1 /* Source of instability if done naively */
    // Build our initial SA_MASTER_TX messages
    if (s->get_local_address() == MASTER_NODE) {
        ts = g_tw_lookahead + tw_rand_unif(&lp->cur_state->rng) * MASTER_SA_INTERVAL + MASTER_SA_INTERVAL;
        e = tw_event_new(lp->gid, ts, lp);
        //e = tw_event_new(sa_master_for_level(lp->gid), ts, lp);
        msg = (olsr_msg_data*)tw_event_data(e);
        msg->type = SA_MASTER_TX;
        msg->originator = s->get_local_address();
        // Always send these to node zero, who receives all SA_MASTER msgs
        msg->destination = sa_master_for_level(lp->gid);
        msg->lng = s->get_lng();
        msg->lat = s->get_lat();
        tw_event_send(e);
    }
#endif
}

void sa_master_init(node_state *s, tw_lp *lp)
{
#if DEBUG
  fprintf( olsr_event_log, "SA Master Init LP %llu RNG Seeds Are: ", lp->gid);
  rng_write_state( lp->rng, olsr_event_log );
#endif

    s->set_local_address(lp->gid);
    //printf("I am an SA master and my local_address is %lu\n", s->local_address);
}

/**
 Copied from ns3 - propogation-loss-model.cc
 */
double
DoCalcRxPower (double txPowerDbm,
               node_state *s,
               olsr_msg_data *m)
{
    /*
     * Friis free space equation:
     * where Pt, Gr, Gr and P are in Watt units
     * L is in meter units.
     *
     *    P     Gt * Gr * (lambda^2)
     *   --- = ---------------------
     *    Pt     (4 * pi * d)^2 * L
     *
     * Gt: tx gain (unit-less)
     * Gr: rx gain (unit-less)
     * Pt: tx power (W)
     * d: distance (m)
     * L: system loss
     * lambda: wavelength (m)
     *
     * Here, we ignore tx and rx gain and the input and output values
     * are in dB or dBm:
     *
     *                           lambda^2
     * rx = tx +  10 log10 (-------------------)
     *                       (4 * pi * d)^2 * L
     *
     * rx: rx power (dB)
     * tx: tx power (dB)
     * d: distance (m)
     * L: system loss (unit-less)
     * lambda: wavelength (m)
     */

    double sender_lng = m->lng;
    double sender_lat = m->lat;
    double receiver_lng = s->get_lng();
    double receiver_lat = s->get_lat();

    // We have to make sure that everyone is in the same region even though
    // they may have overlapping x/y coordinates, i.e. a region describes the
    // local plane of existence for the nodes
    assert(region(s->get_local_address()) == region(m->originator));

    double distance = (sender_lng - receiver_lng) * (sender_lng - receiver_lng);
    distance += (sender_lat - receiver_lat) * (sender_lat - receiver_lat);

    distance = sqrt(distance);

    //double distance = a->GetDistanceFrom (b);
    double m_minDistance = 1.0; // A reasonable default
    if (distance <= m_minDistance)
    {
        return txPowerDbm;
    }
    //double m_lambda = 3.0e8 / 2437000000.0; // (2.437 GHz, chan 6)
    double m_lambda = 0.058; // Stolen from Ken's slides, ~ 5GHz
    double numerator = m_lambda * m_lambda;
    double denominator = 16 * M_PI * M_PI * distance * distance;// * m_systemLoss;
    double pr = 10 * log10 (numerator / denominator);
    //printf("distance=%fm, attenuation coefficient=%fdB\n", distance, pr);
    return txPowerDbm + pr;
}

#define RANGE 60.0

int out_of_radio_range(node_state *s, olsr_msg_data *m)
{
#if USE_RADIO_DISTANCE
    const double range = RANGE;

    double sender_lng = m->lng;
    double sender_lat = m->lat;
    double receiver_lng = s->get_lng();
    double receiver_lat = s->get_lat();

    // We have to make sure that everyone is in the same region even though
    // they may have overlapping x/y coordinates, i.e. a region describes the
    // local plane of existence for the nodes
    assert(region(s->get_local_address()) == region(m->originator));

    double dist = (sender_lng - receiver_lng) * (sender_lng - receiver_lng);
    dist += (sender_lat - receiver_lat) * (sender_lat - receiver_lat);

    dist = sqrt(dist);

    if (dist > range) {
        return 1;
    }

    return 0;
#else
    if (DoCalcRxPower(OLSR_MPR_POWER, s, m) < -96.0)
        return 1;

    return 0;
#endif
}

/**
 * Compute D(y) as described in the "MPR Computation" section.  Description:
 *
 * The degree of an one hop neighbor node y (where y is a member of N), is
 * defined as the number of symmetric neighbors of node y, EXCLUDING all the
 * members of N and EXCLUDING the node performing the computation.
 *
 * N is the subset of neighbors of the node, which are neighbors of the
 * interface I.
 */
unsigned node_state::Dy(o_addr target) const
{
    o_addr temp[OLSR_MAX_NEIGHBORS];
    int temp_size = 0;

    int num_two_hop = get_num_two_hop();

    int i = 0;

    for (const auto & thn: twoHopSet) {
        if (thn == nullptr) break;
        int in = 0;
        for (const auto & ns: neighSet) {
            if (ns == nullptr) break;
            if (thn->twoHopNeighborAddr == ns->neighborMainAddr) {
                in = 1;
                continue;
            }
        }
        if (in) continue;
        if (thn->neighborMainAddr == target) {
            in = 0;
            for (int j = 0; j < temp_size; j++) {
                if (temp[j] == thn->twoHopNeighborAddr) {
                    in = 1;
                }
            }
            if (!in) {
                temp[temp_size] = thn->twoHopNeighborAddr;
                temp_size++;
                assert(temp_size < OLSR_MAX_NEIGHBORS);
            }
        }
    }

    return temp_size;
}

/**
 * Remove "n" from N2 (stored in g_mpr_two_hop)
 */
void remove_node_from_n2(o_addr n)
{
    int i;
    int index_to_remove;

    while (1) {
        index_to_remove = -1;
        for (i = 0; i < g_num_two_hop; i++) {
            if (g_mpr_two_hop[i].twoHopNeighborAddr == n) {
                index_to_remove = i;
                break;
            }
        }

        if (index_to_remove == -1) break;

        //printf("Removing %d\n", index_to_remove);
        g_mpr_two_hop[index_to_remove] = g_mpr_two_hop[g_num_two_hop-1];
        g_num_two_hop--;
    }
}

/**
 * Ensure that all nodes in MPR set are unique (hence "set")
 */
void mpr_set_uniq(node_state *s)
{
    int i;

    // Presumably if we just added a MPR, we only need to check all the
    // others against the last one
    o_addr last = s->get_MprSet(s->get_num_mpr()-1);

    for (i = 0; i < s->get_num_mpr() - 1; i++) {
        if (s->get_MprSet(i) == last) {
            s->set_num_mpr(s->get_num_mpr() - 1);
            return;
        }
    }
}

/**
 * Ensure that all nodes in MPR selector set are unique (hence "set")
 */
void mpr_sel_set_uniq(node_state *s)
{
    int i;

    // Presumably if we just added a MPR, we only need to check all the
    // others against the last one
    o_addr last = s->get_MprSelSet(s->get_num_mpr_sel()-1).mainAddr;

    for (i = 0; i < s->get_num_mpr_sel() - 1; i++) {
        if (s->get_MprSelSet(i).mainAddr == last) {
            s->set_num_mpr_sel(s->get_num_mpr_sel() - 1);
            return;
        }
    }

    s->set_ansn(s->get_ansn() + 1);
}

/**
 * Direct ripoff of corresponding ns3 function
 */
top_tuple * FindNewerTopologyTuple(o_addr last, uint16_t ansn, node_state *s)
{
    int i;

    for (i = 0; i < s->get_num_top_set(); i++) {
        if (s->get_topSet(i)->lastAddr == last && s->get_topSet(i)->sequenceNumber > ansn)
            return s->get_topSet(i);
    }

    return nullptr;
}

/**
 * Direct ripoff of corresponding ns3 function
 */
void EraseOlderTopologyTuples(o_addr last, uint16_t ansn, node_state *s)
{
    int i;
    int index_to_remove;

    while (1) {
        index_to_remove = -1;
        for (i = 0; i < s->get_num_top_set(); i++) {
            if (s->get_topSet(i)->lastAddr == last && s->get_topSet(i)->sequenceNumber < ansn) {
                index_to_remove = i;
                break;
            }
        }

        if (index_to_remove == -1) break;

        s->get_topSetSP(index_to_remove).swap(s->get_topSetSP(s->get_num_top_set()-1));
        s->set_num_top_set(s->get_num_top_set() - 1);
    }
}

/**
 * Direct ripoff of corresponding ns3 function
 */
top_tuple * node_state::FindTopologyTuple(o_addr destAddr, o_addr lastAddr) const
{
    for (const auto &t: topSet) {
        if (t == nullptr) break;
        if (t->destAddr == destAddr && t->lastAddr == lastAddr)
            return t.get();
    }

    return nullptr;
}

neigh_tuple * node_state::FindSymNeighborTuple(o_addr mainAddr) const
{
    for (const auto &n: neighSet) {
        if (n == nullptr) break;
        if (n->neighborMainAddr == mainAddr)
            return n.get();
    }

    return nullptr;
}

RT_entry * node_state::Lookup(o_addr dest) const
{
    for (const auto & r: route_table) {
        if (r == nullptr) break;
        if (r->destAddr == dest) return r.get();
    }

    return nullptr;
}

/**
 * Trying to ripoff the corresponding ns3 function :)
 * Fortunately we don't need steps 4 or 5 since we don't support
 * multiple interfaces or HNA.
 */
void RoutingTableComputation(node_state *s)
{
    int i, h;
    RT_entry *route;

    // 1. All the entries from the routing table are removed.
    s->set_num_routes(0);

    // 2. The new routing entries are added starting with the
    // symmetric neighbors (h=1) as the destination nodes.
    for (i = 0; i < s->get_num_neigh(); i++) {
        RT_entry nt;
        nt.destAddr = s->get_neighSet(i)->neighborMainAddr;
        nt.nextAddr = s->get_neighSet(i)->neighborMainAddr;
        nt.distance = 1;
        s->set_route_table(s->get_num_routes(), nt);
        s->set_num_routes(s->get_num_routes() + 1);
        assert(s->get_num_routes() < OLSR_MAX_ROUTES);
    }

    //  3. for each node in N2, i.e., a 2-hop neighbor which is not a
    //  neighbor node or the node itself, and such that there exist at
    //  least one entry in the 2-hop neighbor set where
    //  N_neighbor_main_addr correspond to a neighbor node with
    //  willingness different of WILL_NEVER,
    for (i = 0; i < s->get_num_two_hop(); i++) {

        if (s->FindSymNeighborTuple(s->get_twoHopSet(i)->twoHopNeighborAddr)) {
            continue;
        }

        if (s->get_twoHopSet(i)->twoHopNeighborAddr == s->get_local_address()) {
            continue;
        }

        // one selects one 2-hop tuple and creates one entry in the routing table with:
        //                R_dest_addr  =  the main address of the 2-hop neighbor;
        //                R_next_addr  = the R_next_addr of the entry in the
        //                               routing table with:
        //                                   R_dest_addr == N_neighbor_main_addr
        //                                                  of the 2-hop tuple;
        //                R_dist       = 2;
        //                R_iface_addr = the R_iface_addr of the entry in the
        //                               routing table with:
        //                                   R_dest_addr == N_neighbor_main_addr
        //                                                  of the 2-hop tuple;
        if ((route = s->Lookup(s->get_twoHopSet(i)->neighborMainAddr))) {
            RT_entry nt;
            nt.destAddr = s->get_twoHopSet(i)->twoHopNeighborAddr;
            nt.nextAddr = route->nextAddr;
            nt.distance = 2;
            s->set_route_table(s->get_num_routes(), nt);
            s->set_num_routes(s->get_num_routes() + 1);
            assert(s->get_num_routes() < OLSR_MAX_ROUTES);
        }
    }

    // 3.1. For each topology entry in the topology table, if its
    // T_dest_addr does not correspond to R_dest_addr of any
    // route entry in the routing table AND its T_last_addr
    // corresponds to R_dest_addr of a route entry whose R_dist
    // is equal to h, then a new route entry MUST be recorded in
    // the routing table (if it does not already exist)
    for (h = 2; ; h++) {
        int added = 0;

        for (i = 0; i < s->get_num_top_set(); i++) {
            //printf("Looking at node %lu top_tuple[%d] dest: %lu, last: %lu, seq: %d\n", s->local_address, i, s->topSet[i].destAddr, s->topSet[i].lastAddr, s->topSet[i].sequenceNumber);
            RT_entry *destAddrEntry = s->Lookup(s->get_topSet(i)->destAddr);
            RT_entry *lastAddrEntry = s->Lookup(s->get_topSet(i)->lastAddr);
            if (!destAddrEntry && lastAddrEntry && lastAddrEntry->distance == h) {
                RT_entry nt;
                nt.destAddr = s->get_topSet(i)->destAddr;
                nt.nextAddr = lastAddrEntry->nextAddr;
                nt.distance = h + 1;
                s->set_route_table(s->get_num_routes(), nt);
                s->set_num_routes(s->get_num_routes() + 1);
                assert(s->get_num_routes() < OLSR_MAX_ROUTES);
                added = 1;
            }
            else {
                if (lastAddrEntry && !destAddrEntry) {
                    //printf("Not right length!\n");
                }
            }
        }

        if (!added) {
            break;
        }
    }
}

dup_tuple * FindDuplicateTuple(o_addr addr, uint16_t seq_num, node_state *s)
{
    int i;

    for (i = 0; i < s->get_num_dupes(); i++) {
        if (s->get_dupSet(i)->address == addr && s->get_dupSet(i)->sequenceNumber == seq_num) {
            return s->get_dupSet(i);
        }
    }

    return nullptr;
}

/**
 * Had to add this function to minimize our dupe array
 */
void AddDuplicate(o_addr originator,
                  uint16_t seq_num,
                  Time ts,
                  int retransmitted,
                  node_state *s,
                  tw_lp *lp)
{
    int i;
    int index_to_remove;
    Time exp = tw_now(lp);

    while (1) {
        index_to_remove = -1;
        for (i = 0; i < s->get_num_dupes(); i++) {
            if (s->get_dupSet(i)->expirationTime < exp) {
                index_to_remove = i;
                break;
            }
        }

        if (index_to_remove == -1) break;

        //printf("Expiring Dupe\n");

        s->get_dupSetSP(index_to_remove).swap(s->get_dupSetSP(s->get_num_dupes()-1));
        s->set_num_dupes(s->get_num_dupes() - 1);
    }

    if (s->get_num_dupes() == OLSR_MAX_DUPES - 1) {
        // Find the oldest and replace it
        int oldest = 0;

        for (i = 0; i < s->get_num_dupes(); i++) {
            if (s->get_dupSet(i)->expirationTime < s->get_dupSet(oldest)->expirationTime) {
                oldest = i;
            }
        }

        //printf("node %lu (lpid = %llu) evicting dup %d (%lu) at time %f\n", s->local_address, lp->gid,
         //      oldest, s->dupSet[oldest].address, tw_now(lp));

        auto nt = std::allocate_shared<dup_tuple>(dup_tuple_allocator);
        nt->address = originator;
        nt->sequenceNumber = seq_num;
        nt->expirationTime = ts;
        nt->retransmitted = retransmitted;
        s->get_dupSetSP(oldest).swap(nt);
    }
    else {
        dup_tuple nt;
        nt.address = originator;
        nt.sequenceNumber = seq_num;
        nt.expirationTime = ts;
        nt.retransmitted = retransmitted;
        s->set_dupSet(s->get_num_dupes(), nt);
        s->set_num_dupes(s->get_num_dupes() + 1);
        assert(s->get_num_dupes() < OLSR_MAX_DUPES);
    }
}

void printTC(olsr_msg_data *m, node_state *s)
{
#ifdef JML_DEBUG
    int i;

    printf("Node %llu has %d neighbors:\n", s->local_address, s->num_neigh);
    for (i = 0; i < s->num_neigh; i++) {
        printf("   neighbor %lu\n", s->neighSet[i].neighborMainAddr);
    }
    printf("Received TC message with %d neighbors of node %lu\n",
           m->mt.t.num_neighbors, m->originator);
    for (i = 0; i < m->mt.t.num_neighbors; i++) {
        printf("   TC-NEIGH %llu\n", m->mt.t.neighborAddresses[i]);
    }
    printf("\n");

#endif /* JML_DEBUG */

    //printf("Node %lu just heard a TC from %lu\n", s->local_address, m->originator);
}

///
/// \brief OLSR's default forwarding algorithm.
///
/// See RFC 3626 for details.
///
/// \param p the %OLSR packet which has been received.
/// \param msg the %OLSR message which must be forwarded.
/// \param dup_tuple nullptr if the message has never been considered for forwarding,
/// or a duplicate tuple in other case.
/// \param local_iface the address of the interface where the message was received from.
///
void ForwardDefault(olsr_msg_data *olsrMessage,
                    dup_tuple *duplicated,
                    o_addr localIface,
                    o_addr senderAddress,
                    node_state *s,
                    tw_lp *lp)
{
    int i;
    int j;
    TC *t;
    tw_event *e;
    tw_stime ts;
    tw_lp *cur_lp;
    olsr_msg_data *msg;

    // If the sender interface address is not in the symmetric
    // 1-hop neighborhood the message must not be forwarded
    if (nullptr == s->FindSymNeighborTuple(senderAddress)) {
        return;
    }

    // If the message has already been considered for forwarding,
    // it must not be retransmitted again
    if (duplicated != nullptr && duplicated->retransmitted)
    {
        return;
    }

    // If the sender interface address is an interface address
    // of a MPR selector of this node and ttl is greater than 1,
    // the message must be retransmitted
    int retransmitted = 0;
    for (i = 0; i < s->get_num_mpr_sel(); i++) {
        if (s->get_MprSelSet(i).mainAddr == senderAddress) {
            // Round-robin-RX
            // Might want to rename HELLO_DELTA...
            ts = g_tw_lookahead + tw_rand_unif(&lp->cur_state->rng) * HELLO_DELTA;
            // ts += 1;

            cur_lp = tw_getlocal_lp(region(s->get_local_address())*OLSR_MAX_NEIGHBORS);

            e = tw_event_new(cur_lp->gid, ts, lp);
            msg = (olsr_msg_data*)tw_event_data(e);
            msg->type = TC_RX;
            msg->ttl = olsrMessage->ttl - 1;
            msg->originator = olsrMessage->originator;
            msg->sender = s->get_local_address();
            msg->lng = s->get_lng();
            msg->lat = s->get_lat();
            msg->target = region(s->get_local_address()) * OLSR_MAX_NEIGHBORS;
            t = &msg->mt.t;
            t->ansn = olsrMessage->mt.t.ansn;
            //t->num_mpr_sel = olsrMessage->mt.t.num_mpr_sel;
            t->num_neighbors = olsrMessage->mt.t.num_neighbors;
            for (j = 0; j < t->num_neighbors; j++) {
                t->neighborAddresses[j] = olsrMessage->mt.t.neighborAddresses[j];
            }
            //if (t->num_mpr_sel > 0) {
            //printTC(t);
            tw_event_send(e);
            //}
            //tw_event_send(e);


            retransmitted = 1;
            // TODO: Check if ns3 stuff is required here...  Possibly
            // actually do the retransmission here.
        }
    }

    if (duplicated != nullptr) {
        duplicated->expirationTime = tw_now(lp) + OLSR_DUP_HOLD_TIME;
        duplicated->retransmitted = retransmitted;
    }
    else {
      AddDuplicate(olsrMessage->originator,
		   olsrMessage->seq_num,
		   tw_now(lp) + OLSR_DUP_HOLD_TIME,
		   retransmitted,
		   s, lp);

      //        s->dupSet[s->num_dupes].address = olsrMessage->originator;
      //        s->dupSet[s->num_dupes].sequenceNumber = olsrMessage->seq_num;
      //        s->dupSet[s->num_dupes].expirationTime = tw_now(lp) + OLSR_DUP_HOLD_TIME;
      //        s->dupSet[s->num_dupes].retransmitted = retransmitted;
      //        s->num_dupes++;
      //        assert(s->num_dupes < OLSR_MAX_DUPES);
    }
}

void route_packet(node_state *s, tw_event *e)
{
    olsr_msg_data *m = (olsr_msg_data*)tw_event_data(e);
    RT_entry * route = s->Lookup(m->destination);
    if (route == nullptr) {
        printf("Node %llu doesn't have a route to %llu\n", s->get_local_address(), m->destination);
        return;
    }

    m->ttl--;

    //printf("routing from %lu to %lu, next hop %lu\n", m->originator,
    //       m->destination, route->nextAddr);

    m->sender = route->nextAddr;
    tw_event_send(e);
}

void process_sa(node_state *s, olsr_msg_data *m)
{
    s->SA_per_node[m->originator % OLSR_MAX_NEIGHBORS]++;
}

/**
 * Event handler.  Basically covers two events at the moment:
 * - HELLO_TX: HELLO transmit required now, so package up all of our
 * neighbors into a message and send it.  Also schedule our next TX
 * - HELLO_RX: HELLO received so we just pull the neighbor's address from
 * the message.  HELLO_RX messages are generated by the last HELLO_RX
 * message until we have exhausted all neighbors.
 * - TC_TX: Similar to HELLO_TX but for Topology Control
 * - TC_RX: Similar to HELLO_RX but for Topology Control
 */
void olsr_event(node_state *s, tw_bf *bf, olsr_msg_data *m, tw_lp *lp)
{
    int in;
    int i, j, k;
    int is_mpr;
    TC *t;
    hello *h;
    tw_event *e;
    tw_stime ts;
    tw_lp *cur_lp;
    olsr_msg_data *msg;
    //latlng *ll;
    //latlng_cluster *llc;

#if DEBUG
    fprintf( olsr_event_log, "OLSR Event: LP %llu Type %d at TS = %lf RNGs:", lp->gid, m->type, tw_now(lp) );
    rng_write_state( lp->rng, olsr_event_log );

    if( lp->gid == 1023 ) {
        printf("LP DUMP Node %llu on Rank %llu at TS=%lf: S Local Address = %llu, M Type = %d,M Sender = %llu, M Originator = %llu \n",
               lp->gid, g_tw_mynode, tw_now(lp), s->local_address, m->type, m->sender, m->originator );
    }
#endif /* DEBUG */

#if ENABLE_OPTIMISTIC
    if( g_tw_synchronization_protocol == OPTIMISTIC )
      {
	memcpy( &(m->state_copy), s, sizeof(node_state));
      }
#endif

    g_olsr_event_stats[m->type]++;

    switch(m->type) {
        case HELLO_TX:
        {
            ts = g_tw_lookahead + tw_rand_unif(&lp->cur_state->rng) * HELLO_DELTA;

            cur_lp = tw_getlocal_lp(region(s->get_local_address())*OLSR_MAX_NEIGHBORS);

            e = tw_event_new(cur_lp->gid, ts, lp);
            msg = (olsr_msg_data*)tw_event_data(e);
            msg->type = HELLO_RX;
            msg->originator = m->originator;
            msg->lng = s->get_lng();
            msg->lat = s->get_lat();
            msg->target = tw_getlocal_lp(region(s->get_local_address())*OLSR_MAX_NEIGHBORS)->gid;
            h = &msg->mt.h;
            h->num_neighbors = s->get_num_neigh();// + 1;
            //h->neighbor_addrs[0] = s->local_address;
            for (j = 0; j < s->get_num_neigh(); j++) {
                h->neighbor_addrs[j] = s->get_neighSet(j)->neighborMainAddr;
                // If s->neighSet[j].neighborMainAddr is our MPR, we need
                // to set this appropriately
                is_mpr = 0;
                for (k = 0; k < s->get_num_mpr(); k++) {
                    if (s->get_MprSet(k) == s->get_neighSet(j)->neighborMainAddr) {
                        is_mpr = 1;
                    }
                }
                if (is_mpr) {
                    h->is_mpr[j] = 1;
                }
                else {
                    h->is_mpr[j] = 0;
                }
            }
            tw_event_send(e);

            e = tw_event_new(lp->gid, HELLO_INTERVAL, lp);
            msg = (olsr_msg_data*)tw_event_data(e);
            msg->type = HELLO_TX;
            msg->originator = s->get_local_address();
            msg->lng = s->get_lng();
            msg->lat = s->get_lat();
            h = &msg->mt.h;
            h->num_neighbors = 0;//1;
            //h->neighbor_addrs[0] = s->local_address;
            tw_event_send(e);

            break;
        }
        case HELLO_RX:
        {
            h = &m->mt.h;

            in = 0;

            // If we receive our own message, don't add ourselves but
            // DO generate a new event for the next guy!

            // Copy the message we just received; we can't add data to
            // a message sent by another node
            if (m->target < region(s->get_local_address())*OLSR_MAX_NEIGHBORS+OLSR_MAX_NEIGHBORS-1) {
                ts = g_tw_lookahead + tw_rand_unif(&lp->cur_state->rng) * HELLO_DELTA;

                tw_lp *cur_lp = tw_getlocal_lp(m->target + 1);

                e = tw_event_new(cur_lp->gid, ts, lp);
                msg = (olsr_msg_data*)tw_event_data(e);
                msg->type = HELLO_RX;
                msg->originator = m->originator;
                msg->sender = m->sender;
                msg->lng = m->lng;
                msg->lat = m->lat;
                msg->target = m->target + 1;
                h = &msg->mt.h;
                h->num_neighbors = m->mt.h.num_neighbors;//m->num_neigh + 1;
                //h->neighbor_addrs[0] = s->local_address;
                for (j = 0; j < h->num_neighbors; j++) {
                    h->neighbor_addrs[j] = m->mt.h.neighbor_addrs[j];
                    //h->neighbor_addrs[j] = s->neighSet[j].neighborMainAddr;
                }
                tw_event_send(e);
            }

            // We've already passed along the message which has to happen
            // regardless of whether or not it can be heard, handled, etc.

            // Check to see if we can hear this message or not
            if (out_of_radio_range(s, m)) {
                //printf("Out of range!\n");
                return;
            }

            if (s->get_local_address() == m->originator) {
                return;
            }

            // BEGIN 1-HOP PROCESSING
            for (i = 0; i < s->get_num_neigh(); i++) {
                if (s->get_neighSet(i)->neighborMainAddr == m->originator) {
                    in = 1;
                }
            }

            if (!in) {
                neigh_tuple nt;
                nt.neighborMainAddr = m->originator;
                s->set_neighSet(s->get_num_neigh(), nt);
                s->set_num_neigh(s->get_num_neigh() + 1);
                assert(s->get_num_neigh() < OLSR_MAX_NEIGHBORS);
                assert(region(s->get_local_address()) == region(m->originator));
                s->set_ansn(s->get_ansn() + 1);
            }
            // END 1-HOP PROCESSING

            // BEGIN 2-HOP PROCESSING

            h = &m->mt.h;

            for (i = 0; i < h->num_neighbors; i++) {
                if (s->get_local_address() == h->neighbor_addrs[i]) {
                    // We are not going to be our own 2-hop neighbor!
                    continue;
                }

                // Check and see if h->neighbor_addrs[i] is in our list
                // already
                in = 0;
                for (j = 0; j < s->get_num_two_hop(); j++) {
                    if (s->get_twoHopSet(j)->neighborMainAddr == m->originator &&
                        s->get_twoHopSet(j)->twoHopNeighborAddr == h->neighbor_addrs[i]) {
                        in = 1;
                    }
                }

                if (!in) {
                    two_hop_neigh_tuple nt;
                    nt.neighborMainAddr = m->originator;
                    nt.twoHopNeighborAddr = h->neighbor_addrs[i];
                    s->set_twoHopSet(s->get_num_two_hop(), nt);
                    assert(s->get_twoHopSet(s->get_num_two_hop())->neighborMainAddr !=
                           s->get_twoHopSet(s->get_num_two_hop())->twoHopNeighborAddr);
                    s->set_num_two_hop(s->get_num_two_hop() + 1);
                    assert(s->get_num_two_hop() < OLSR_MAX_2_HOP);
                }
            }

            // END 2-HOP PROCESSING

            // BEGIN MPR COMPUTATION

            // Initially no nodes are covered
            memset(g_covered, 0, BITNSLOTS(OLSR_MAX_NEIGHBORS));
            s->set_num_mpr(0);

            // Copy all relevant information to scratch space
            g_num_one_hop = s->get_num_neigh();
            for (i = 0; i < g_num_one_hop; i++) {
                g_mpr_one_hop[i] = *s->get_neighSet(i);
            }

            g_num_two_hop = s->get_num_two_hop();
            for (i = 0; i < g_num_two_hop; i++) {
                g_mpr_two_hop[i] = *s->get_twoHopSet(i);
            }

            // Calculate D(y), where y is a member of N, for all nodes in N
            for (i = 0; i < g_num_one_hop; i++) {
                g_Dy[i] = s->Dy(g_mpr_one_hop[i].neighborMainAddr);
                g_reachability[i] = 0;
            }

            // Take care of the "unused" bits
//            for (i = g_num_two_hop; i < OLSR_MAX_2_HOP; i++) {
//                BITSET(g_covered, i);
//            }

//            // 2. Calculate D(y), where y is a member of N, for all nodes in N.
//            for (i = 0; i < g_num_one_hop; i++) {
//                g_Dy[i] = Dy(s, s->neighSet[i].neighborMainAddr);
//            }

            // 3. Add to the MPR set those nodes in N, which are the *only*
            // nodes to provide reachability to a node in N2.
//            std::set<Ipv4Address> coveredTwoHopNeighbors;
//            for (TwoHopNeighborSet::const_iterator twoHopNeigh = N2.begin (); twoHopNeigh != N2.end (); twoHopNeigh++)
//            {
//                bool onlyOne = true;
//                // try to find another neighbor that can reach twoHopNeigh->twoHopNeighborAddr
//                for (TwoHopNeighborSet::const_iterator otherTwoHopNeigh = N2.begin (); otherTwoHopNeigh != N2.end (); otherTwoHopNeigh++)
//                {
//                    if (otherTwoHopNeigh->twoHopNeighborAddr == twoHopNeigh->twoHopNeighborAddr
//                        && otherTwoHopNeigh->neighborMainAddr != twoHopNeigh->neighborMainAddr)
//                    {
//                        onlyOne = false;
//                        break;
//                    }
//                }
//                if (onlyOne)
//                {
//                    NS_LOG_LOGIC ("Neighbor " << twoHopNeigh->neighborMainAddr
//                                  << " is the only that can reach 2-hop neigh. "
//                                  << twoHopNeigh->twoHopNeighborAddr
//                                  << " => select as MPR.");
//
//                    mprSet.insert (twoHopNeigh->neighborMainAddr);
//
//                    // take note of all the 2-hop neighbors reachable by the newly elected MPR
//                    for (TwoHopNeighborSet::const_iterator otherTwoHopNeigh = N2.begin ();
//                         otherTwoHopNeigh != N2.end (); otherTwoHopNeigh++)
//                    {
//                        if (otherTwoHopNeigh->neighborMainAddr == twoHopNeigh->neighborMainAddr)
//                        {
//                            coveredTwoHopNeighbors.insert (otherTwoHopNeigh->twoHopNeighborAddr);
//                        }
//                    }
//                }
//            }

            for (i = 0; i < g_num_two_hop; i++) {
                int onlyOne = 1;
                // try to find another neighbor that can reach twoHopNeigh->twoHopNeighborAddr
                for (j = 0; j < g_num_two_hop; j++) {
                    if (g_mpr_two_hop[j].twoHopNeighborAddr == g_mpr_two_hop[i].twoHopNeighborAddr
                        && g_mpr_two_hop[j].neighborMainAddr != g_mpr_two_hop[i].neighborMainAddr) {
                        onlyOne = 0;
                        break;
                    }
                }

                if (onlyOne) {
                    s->set_MprSet(s->get_num_mpr(), g_mpr_two_hop[i].neighborMainAddr);
                    s->set_num_mpr(s->get_num_mpr() + 1);
                    assert(s->get_num_mpr() < OLSR_MAX_NEIGHBORS);
                    // Make sure they're all unique!
                    mpr_set_uniq(s);

                    // take note of all the 2-hop neighbors reachable by the newly elected MPR
                    for (j = 0; j < g_num_two_hop; j++) {
                        if (g_mpr_two_hop[j].neighborMainAddr == g_mpr_two_hop[i].neighborMainAddr) {
                            //coveredTwoHopNeighbors.insert (otherTwoHopNeigh->twoHopNeighborAddr);
                            // We don't do that, we use bitfields.  Make sure
                            // our assumptions are correct then create a mask
                            //printf("%lu\n", g_mpr_two_hop[j].neighborMainAddr);
                            assert(region(g_mpr_two_hop[j].neighborMainAddr) == region(s->get_local_address()));
                            BITSET(g_covered, g_mpr_two_hop[j].twoHopNeighborAddr % OLSR_MAX_NEIGHBORS);
                        }
                    }
                }
            }

//            // Remove the nodes from N2 which are now covered by a node in the MPR set.
//            for (TwoHopNeighborSet::iterator twoHopNeigh = N2.begin ();
//                 twoHopNeigh != N2.end (); )
//            {
//                if (coveredTwoHopNeighbors.find (twoHopNeigh->twoHopNeighborAddr) != coveredTwoHopNeighbors.end ())
//                {
//                    // This works correctly only because it is known that twoHopNeigh is reachable by exactly one neighbor,
//                    // so only one record in N2 exists for each of them. This record is erased here.
//                    NS_LOG_LOGIC ("2-hop neigh. " << twoHopNeigh->twoHopNeighborAddr << " is already covered by an MPR.");
//                    twoHopNeigh = N2.erase (twoHopNeigh);
//                }
//                else
//                {
//                    twoHopNeigh++;
//                }
//            }
            // Remove the nodes from N2 which are now covered by a node in the MPR set.
            for (i = 0; i < g_num_two_hop; i++) {
                if (BITTEST(g_covered, g_mpr_two_hop[i].twoHopNeighborAddr % OLSR_MAX_NEIGHBORS)) {
                    //printf("1. g_num_two_hop is %d\n", g_num_two_hop);
                    remove_node_from_n2(g_mpr_two_hop[i].twoHopNeighborAddr);
                    //printf("2. g_num_two_hop is %d\n", g_num_two_hop);
                }
            }

            //return;

//            // 4. While there exist nodes in N2 which are not covered by at
//            // least one node in the MPR set:
//            while (N2.begin () != N2.end ())
            //printf("\n\n");
            while (g_num_two_hop) {
                //printf(".");
//                // 4.1. For each node in N, calculate the reachability, i.e., the
//                // number of nodes in N2 which are not yet covered by at
//                // least one node in the MPR set, and which are reachable
//                // through this 1-hop neighbor
//                std::map<int, std::vector<const NeighborTuple *> > reachability;
//                std::set<int> rs;
//                for (NeighborSet::iterator it = N.begin (); it != N.end (); it++)
//                {
//                    NeighborTuple const &nb_tuple = *it;
//                    int r = 0;
//                    for (TwoHopNeighborSet::iterator it2 = N2.begin (); it2 != N2.end (); it2++)
//                    {
//                        TwoHopNeighborTuple const &nb2hop_tuple = *it2;
//                        if (nb_tuple.neighborMainAddr == nb2hop_tuple.neighborMainAddr)
//                            r++;
//                    }
//                    rs.insert (r);
//                    reachability[r].push_back (&nb_tuple);
//                }

                for (i = 0; i < g_num_one_hop; i++) {
                    int r = 0;

                    for (j = 0; j < g_num_two_hop; j++) {
                        if (g_mpr_one_hop[i].neighborMainAddr == g_mpr_two_hop[j].neighborMainAddr)
                            r++;
                    }
                    // Make sure our neighbors are from our region
                    assert(region(g_mpr_one_hop[i].neighborMainAddr) == region(s->get_local_address()));
                    g_reachability[i] = r;
                }

//                // 4.2. Select as a MPR the node with highest N_willingness among
//                // the nodes in N with non-zero reachability. In case of
//                // multiple choice select the node which provides
//                // reachability to the maximum number of nodes in N2. In
//                // case of multiple nodes providing the same amount of
//                // reachability, select the node as MPR whose D(y) is
//                // greater. Remove the nodes from N2 which are now covered
//                // by a node in the MPR set.
//                NeighborTuple const *max = nullptr;
//                int max_r = 0;
//                for (std::set<int>::iterator it = rs.begin (); it != rs.end (); it++)
//                {
//                    int r = *it;
//                    if (r == 0)
//                    {
//                        continue;
//                    }
//                    for (std::vector<const NeighborTuple *>::iterator it2 = reachability[r].begin ();
//                         it2 != reachability[r].end (); it2++)
//                    {
//                        const NeighborTuple *nb_tuple = *it2;
//                        if (max == nullptr || nb_tuple->willingness > max->willingness)
//                        {
//                            max = nb_tuple;
//                            max_r = r;
//                        }
//                        else if (nb_tuple->willingness == max->willingness)
//                        {
//                            if (r > max_r)
//                            {
//                                max = nb_tuple;
//                                max_r = r;
//                            }
//                            else if (r == max_r)
//                            {
//                                if (Degree (*nb_tuple) > Degree (*max))
//                                {
//                                    max = nb_tuple;
//                                    max_r = r;
//                                }
//                            }
//                        }
//                    }
//                }

                int max = 0;
                int max_Dy = 0;

                for (i = 0; i < g_num_one_hop; i++) {
                    if (g_reachability[i] == 0) continue;

                    if (g_reachability[i] > max) {
                        max = g_reachability[i];
                        g_mpr_neigh_to_add = g_mpr_one_hop[i];
                        max_Dy = g_Dy[i];
                    }
                    else if (g_reachability[i] == max) {
                        if (g_Dy[i] > max_Dy) {
                            max = g_reachability[i];
                            g_mpr_neigh_to_add = g_mpr_one_hop[i];
                            max_Dy = g_Dy[i];
                        }
                    }
                }

                if (max > 0) {
                    s->set_MprSet(s->get_num_mpr(), g_mpr_neigh_to_add.neighborMainAddr);
                    s->set_num_mpr(s->get_num_mpr() + 1);
                    assert(s->get_num_mpr() < OLSR_MAX_NEIGHBORS);
                    // Make sure they're all unique!
                    mpr_set_uniq(s);

                    // take note of all the 2-hop neighbors reachable by the newly elected MPR
                    for (j = 0; j < g_num_two_hop; j++) {
                        if (g_mpr_two_hop[j].neighborMainAddr == g_mpr_neigh_to_add.neighborMainAddr) {
                            //coveredTwoHopNeighbors.insert (otherTwoHopNeigh->twoHopNeighborAddr);
                            // We don't do that, we use bitfields.  Make sure
                            // our assumptions are correct then create a mask
                            assert(region(g_mpr_two_hop[j].neighborMainAddr) == region(s->get_local_address()));
                            BITSET(g_covered, g_mpr_two_hop[j].twoHopNeighborAddr % OLSR_MAX_NEIGHBORS);
                        }
                    }
                }

                // Remove the nodes from N2 which are now covered by a node in the MPR set.
                for (i = 0; i < g_num_two_hop; i++) {
                    if (BITTEST(g_covered, g_mpr_two_hop[i].twoHopNeighborAddr % OLSR_MAX_NEIGHBORS)) {
                        //printf("1. g_num_two_hop is %d\n", g_num_two_hop);
                        remove_node_from_n2(g_mpr_two_hop[i].twoHopNeighborAddr);
                        //printf("2. g_num_two_hop is %d\n", g_num_two_hop);
                    }
                }

            }

            // END MPR COMPUTATION

            // BEGIN MPR SELECTOR SET

            h = &m->mt.h;

            for (i = 0; i < h->num_neighbors; i++) {
                if (h->is_mpr[i]) {
                    // Check if it contains OUR address
                    if (h->neighbor_addrs[i] == s->get_local_address()) {
                        // We should add this guy to the selector set
                        mpr_sel_tuple nt;
                        nt.mainAddr = m->originator;
                        s->set_MprSelSet(s->get_num_mpr_sel(), nt);
                        s->set_num_mpr_sel(s->get_num_mpr_sel() + 1);
                        assert(s->get_num_mpr_sel() <= OLSR_MAX_NEIGHBORS);
                        mpr_sel_set_uniq(s);
                    }
                }
            }

            // END MPR SELECTOR SET

            break;
        }
        case TC_TX:
        {
            // Might want to rename HELLO_DELTA...
            ts = g_tw_lookahead + tw_rand_unif(&lp->cur_state->rng) * HELLO_DELTA;

            cur_lp = tw_getlocal_lp(region(s->get_local_address())*OLSR_MAX_NEIGHBORS);

            e = tw_event_new(cur_lp->gid, ts, lp);
            msg = (olsr_msg_data*)tw_event_data(e);
            msg->type = TC_RX;
            msg->ttl = 255;
            msg->originator = m->originator;
            msg->sender = s->get_local_address();
            msg->lng = s->get_lng();
            msg->lat = s->get_lat();
            msg->target = tw_getlocal_lp(region(s->get_local_address())*OLSR_MAX_NEIGHBORS)->gid;
            t = &msg->mt.t;
            t->ansn = s->get_ansn();
            //t->num_mpr_sel = s->num_mpr_sel;
            t->num_neighbors = s->get_num_neigh();
            for (j = 0; j < s->get_num_neigh(); j++) {
                t->neighborAddresses[j] = s->get_neighSet(j)->neighborMainAddr;
            }
            //if (s->num_mpr_sel > 0) {
            //printTC(t);
            tw_event_send(e);
            //}
            //tw_event_send(e);

            e = tw_event_new(lp->gid, TC_INTERVAL, lp);
            msg = (olsr_msg_data*)tw_event_data(e);
            msg->type = TC_TX;
            msg->originator = s->get_local_address();
            msg->lng = s->get_lng();
            msg->lat = s->get_lat();
            t = &msg->mt.t;
            //t->num_mpr_sel = 0;
            t->num_neighbors = 0;
            //printTC(t);
            tw_event_send(e);

            break;
        }
        case TC_RX:
        {
            // 1. When a packet arrives at the router, the router evaluates the TTL.
            // 2a. If the TTL=0, the router drops the packet, and sends an ICMP TTL Exceeded message to the source.
            // 2b. If the TTL>0, then the router decrements the TTL by 1, and then forwards the packet.
            // From http://www.dslreports.com/forum/r25655787-When-is-TTL-Decremented-by-a-Router-

            if (m->ttl == 0) {
                printf("TC_RX\n");
                printf("TTL Expired\n");
                return;
            }

            m->ttl--;

            // Copy the message we just received; we can't add data to
            // a message sent by another node

            if (m->target < region(s->get_local_address())*OLSR_MAX_NEIGHBORS+OLSR_MAX_NEIGHBORS-1) {
                ts = g_tw_lookahead + tw_rand_unif(&lp->cur_state->rng) * HELLO_DELTA;

                tw_lp *cur_lp = tw_getlocal_lp(m->target + 1);

                e = tw_event_new(cur_lp->gid, ts, lp);
                msg = (olsr_msg_data*)tw_event_data(e);
                msg->type = TC_RX;
                msg->ttl = m->ttl;
                msg->originator = m->originator;
                msg->sender = m->sender;
                msg->lng = m->lng;
                msg->lat = m->lat;
                msg->target = m->target + 1;
                t = &msg->mt.t;
                t->ansn = m->mt.t.ansn;
                //t->num_mpr_sel = m->mt.t.num_mpr_sel;
                t->num_neighbors = m->mt.t.num_neighbors;
                for (j = 0; j < t->num_neighbors; j++) {
                    t->neighborAddresses[j] = m->mt.t.neighborAddresses[j];
                }
                //printTC(t);
                tw_event_send(e);
            }

            // We've already passed along the message which has to happen
            // regardless of whether or not it can be heard, handled, etc.

            // Check to see if we can hear this message or not
            if (out_of_radio_range(s, m)) {
                //printf("Out of range!\n");
                return;
            }

            if (s->get_local_address() == m->originator) {
                return;
            }

            // BEGIN TC PROCESSING

            //int do_forwarding = 1;
            dup_tuple *duplicated = FindDuplicateTuple(m->originator, m->seq_num, s);

            if (duplicated != nullptr) {
                //break;
            }

            ForwardDefault(m, duplicated, s->get_local_address(), m->sender, s, lp);

            in = 0;

            // 1. If the sender interface of this message is not in the symmetric
            // 1-hop neighborhood of this node, the message MUST be discarded.
            for (i = 0; i < s->get_num_neigh(); i++) {
                if (m->sender == s->get_neighSet(i)->neighborMainAddr)
                    in = 1;
            }

            if (!in)
                return;

            // 2. If there exist some tuple in the topology set where:
            //    T_last_addr == originator address AND
            //    T_seq       >  ANSN,
            // then further processing of this TC message MUST NOT be
            // performed.
            top_tuple *tt = FindNewerTopologyTuple(m->originator, m->mt.t.ansn, s);
            if (tt != nullptr)
                return;

            // 3. All tuples in the topology set where:
            //	T_last_addr == originator address AND
            //	T_seq       <  ANSN
            // MUST be removed from the topology set.
            EraseOlderTopologyTuples(m->originator, m->mt.t.ansn, s);

            printTC(m, s);

            // 4. For each of the advertised neighbor main address received in
            // the TC message:
            for (i = 0; i < m->mt.t.num_neighbors; i++) {
                o_addr addr = m->mt.t.neighborAddresses[i];
                // 4.1. If there exist some tuple in the topology set where:
                //        T_dest_addr == advertised neighbor main address, AND
                //        T_last_addr == originator address,
                // then the holding time of that tuple MUST be set to:
                //        T_time      =  current time + validity time.
                tt = s->FindTopologyTuple(addr, m->originator);

                if (tt != nullptr) {
#warning "Correct this line - TOP_HOLD_TIME should be in the struct!"
                    tt->expirationTime = tw_now(lp) + TOP_HOLD_TIME;
                }
                else {
                    // 4.2. Otherwise, a new tuple MUST be recorded in the topology
                    // set where:
                    //	T_dest_addr = advertised neighbor main address,
                    //	T_last_addr = originator address,
                    //	T_seq       = ANSN,
                    //	T_time      = current time + validity time.
                    top_tuple nt;
                    nt.destAddr = addr;
                    nt.lastAddr = m->originator;
                    nt.sequenceNumber = m->mt.t.ansn;
                    nt.expirationTime = tw_now(lp) + TOP_HOLD_TIME;
                    s->set_topSet(s->get_num_top_set(), nt);
#warning "Correct this line - TOP_HOLD_TIME should be in the struct!"
                    s->set_num_top_set(s->get_num_top_set() + 1);
                    assert(s->get_num_top_set() < OLSR_MAX_TOP_TUPLES);
                }
            }

            // END TC PROCESSING


            break;
        }
        case SA_TX:
        {
            /*
             Situational awareness - every 10 seconds send a UDP packet
             containing a node's location to a specified node with an
             uplink.  Every 60 seconds, that uplink sends a message containing
             all nodes' locations.
             */
            // Schedule ourselves again...
            ts = SA_INTERVAL;
            e = tw_event_new(lp->gid, ts, lp);
            msg = (olsr_msg_data*)tw_event_data(e);
            msg->type = SA_TX;
            msg->originator = s->get_local_address();
            msg->destination = MASTER_NODE;
            msg->lng = s->get_lng();
            msg->lat = s->get_lat();
            tw_event_send(e);


            // Check and see if we are the destination...
            if (m->destination == s->get_local_address()) {
                // This is the final stop
                process_sa(s, m);
                return;
            }

            // Might want to rename HELLO_DELTA...
            ts = g_tw_lookahead + tw_rand_unif(&lp->cur_state->rng) * HELLO_DELTA;

            cur_lp = tw_getlocal_lp(region(s->get_local_address())*OLSR_MAX_NEIGHBORS);

            // If we don't have a route, don't allocate an event!
            if (s->Lookup(MASTER_NODE) == nullptr) {
                return;
            }

            e = tw_event_new(cur_lp->gid, ts, lp);
            msg = (olsr_msg_data*)tw_event_data(e);
            msg->type = SA_RX;
            msg->ttl = 255;
            msg->originator = s->get_local_address();
            msg->sender = s->get_local_address();
            msg->destination = MASTER_NODE;
            msg->lng = s->get_lng();
            msg->lat = s->get_lat();
            msg->target = region(s->get_local_address()) * OLSR_MAX_NEIGHBORS;

            route_packet(s, e);

            // We don't need to compute our routing table here so just return!
            return;
        }
        case SA_RX:
        {
            // 1. When a packet arrives at the router, the router evaluates the TTL.
            // 2a. If the TTL=0, the router drops the packet, and sends an ICMP TTL Exceeded message to the source.
            // 2b. If the TTL>0, then the router decrements the TTL by 1, and then forwards the packet.
            // From http://www.dslreports.com/forum/r25655787-When-is-TTL-Decremented-by-a-Router-

            if (m->ttl == 0) {
                printf("SA_RX\n");
                printf("TTL Expired\n");
                return;
            }

            // Check and see if we are the destination...
            if (m->destination == s->get_local_address()) {
                // This is the final stop
                process_sa(s, m);
                return;
            }

            // Copy the message we just received; we can't add data to
            // a message sent by another node

            if (m->target < region(s->get_local_address())*OLSR_MAX_NEIGHBORS+OLSR_MAX_NEIGHBORS-1) {
                ts = g_tw_lookahead + tw_rand_unif(&lp->cur_state->rng) * HELLO_DELTA;

                tw_lp *cur_lp = tw_getlocal_lp(m->target + 1);

                e = tw_event_new(cur_lp->gid, ts, lp);
                msg = (olsr_msg_data*)tw_event_data(e);
                msg->type = SA_RX;
                msg->ttl = m->ttl;
                msg->originator = m->originator;
                msg->sender = m->sender;
                msg->destination = m->destination;
                msg->lng = m->lng;
                msg->lat = m->lat;
                msg->target = m->target + 1;
                t = &msg->mt.t;
                t->ansn = m->mt.t.ansn;
                //t->num_mpr_sel = m->mt.t.num_mpr_sel;
                t->num_neighbors = m->mt.t.num_neighbors;
                for (j = 0; j < t->num_neighbors; j++) {
                    t->neighborAddresses[j] = m->mt.t.neighborAddresses[j];
                }
                //printTC(t);
                tw_event_send(e);
            }

            // We've already passed along the message which has to happen
            // regardless of whether or not it can be heard, handled, etc.

            // Check to see if we can hear this message or not
            if (out_of_radio_range(s, m)) {
                //printf("Out of range!\n");
                return;
            }

            if (s->get_local_address() == m->originator) {
                return;
            }

            if (m->sender == s->get_local_address()) {
                // If we don't have a route, don't allocate an event!
                if (s->Lookup(MASTER_NODE) == nullptr) {
                    return;
                }

                ts = g_tw_lookahead + tw_rand_unif(&lp->cur_state->rng) * HELLO_DELTA;
                e = tw_event_new(lp->gid, ts, lp);
                msg = (olsr_msg_data*)tw_event_data(e);
                msg->type = SA_RX;
                msg->ttl = m->ttl;
                msg->originator = m->originator;
                msg->sender = s->get_local_address();
                msg->destination = MASTER_NODE;
                msg->lng = s->get_lng();
                msg->lat = s->get_lat();

                route_packet(s, e);
            }


            // We don't need to compute our routing table here so just return!
            return;
        }
#if 0 /* Source of instability if done naively */
            // Build our initial SA_MASTER_TX messages
            if (s->local_address == MASTER_NODE) {
                ts = tw_rand_unif(lp->rng) * MASTER_SA_INTERVAL + MASTER_SA_INTERVAL;
                e = tw_event_new(lp->gid, ts, lp);
                //e = tw_event_new(sa_master_for_level(lp->gid), ts, lp);
                msg = tw_event_data(e);
                msg->type = SA_MASTER_TX;
                msg->originator = s->local_address;
                // Always send these to node zero, who receives all SA_MASTER msgs
                msg->destination = sa_master_for_level(lp->gid, 0);
                msg->lng = s->lng;
                msg->lat = s->lat;
                tw_event_send(e);
            }
#endif
        case SA_MASTER_TX:
        {
            int total_nodes = SA_range_start * tw_nnodes();
            int total_regions = total_nodes / OLSR_MAX_NEIGHBORS;

            //printf("RECEIVED SA_MASTER_TX VALIDLY\n");
            //fflush(stdout);
            // Schedule ourselves again...
            ts = MASTER_SA_INTERVAL + tw_rand_unif(&lp->cur_state->rng);
            e = tw_event_new(lp->gid, ts, lp);
            msg = (olsr_msg_data*)tw_event_data(e);
            msg->type = SA_MASTER_TX;
            msg->originator = s->get_local_address();
            // Always send these to node zero, who receives all SA_MASTER msgs
            msg->destination = sa_master_for_level(lp->gid);
            msg->lng = s->get_lng();
            msg->lat = s->get_lat();
            tw_event_send(e);

            // Send a new SA_MASTER_RX to an SA Master
            ts = 1.0 + tw_rand_unif(&lp->cur_state->rng);
            e = tw_event_new(sa_master_for_level(lp->gid), ts, lp);
            msg = (olsr_msg_data*)tw_event_data(e);
            msg->type = SA_MASTER_RX;
            msg->originator = s->get_local_address();
            msg->sender = s->get_local_address();
            msg->destination = sa_master_for_level(lp->gid);
            msg->level = 0;

#if DEBUG
	    fprintf(olsr_event_log, "Send Event OLSR LP %llu to SA %llu, Type %d at TS = %lf \n",
		    lp->gid, sa_master_for_level(lp->gid), m->type, ts );
#endif

            tw_event_send(e);

//            for (i = 0; i < total_regions; i++) {
//                if (s->local_address == total_nodes + i) {
//                    printf("Don't send a satellite message to ourselves! %lu->%d\n",
//                           s->local_address, total_nodes + i);
//                    continue;
//                }
//                // 2 second round-trip for satellite communications
//                ts = 2.0 + tw_rand_unif(lp->rng);
//                e = tw_event_new(total_nodes + i, ts, lp);
//                msg = tw_event_data(e);
//                msg->type = SA_MASTER_RX;
//                tw_event_send(e);
//            }

//            // Send it on to node 0
//            ts = g_tw_lookahead + tw_rand_unif(lp->rng) * HELLO_DELTA;
//            e = tw_event_new(sa_master_for_level(lp->gid), ts, lp);
//            msg = tw_event_data(e);
//            msg->type = SA_MASTER_RX;
//            msg->originator = s->local_address;
//            msg->sender = s->local_address;
//            msg->destination = sa_master_for_level(lp->gid);
//            msg->lng = s->lng;
//            msg->lat = s->lat;
//            tw_event_send(e);

            return;
        }
        case SA_MASTER_RX:
        {
            //printf("RECEIVED SA_MASTER_RX in ERROR\n");
            //fflush(stdout);
            return;
        }
        case RWALK_CHANGE:
        {
            //printf("Changing our location to %f, %f\n",
            //       m->lng, m->lat);
            s->set_lng(m->lng);
            s->set_lat(m->lat);

            // Build our initial RWALK_CHANGE messages
            ts = tw_rand_unif(&lp->cur_state->rng) * RWALK_INTERVAL + 1.0;
            e = tw_event_new(lp->gid, ts, lp);
            msg = (olsr_msg_data*)tw_event_data(e);
            msg->type = RWALK_CHANGE;
            msg->lng = tw_rand_unif(&lp->cur_state->rng) * GRID_MAX;
            msg->lat = tw_rand_unif(&lp->cur_state->rng) * GRID_MAX;
            tw_event_send(e);
        }

        default:
            return;
    }

    RoutingTableComputation(s);
}

tw_peid olsr_map(tw_lpid gid);

void sa_master_event(node_state *s, tw_bf *bf, olsr_msg_data *m, tw_lp *lp)
{
//    int i;
    tw_stime ts;
    tw_event *e;
    olsr_msg_data *msg;
    tw_lpid dest;
    double x;
//    int total_nodes = SA_range_start * tw_nnodes();
//    int total_regions = total_nodes / OLSR_MAX_NEIGHBORS;

#if DEBUG
    fprintf( olsr_event_log, "SA Master Event: LP %llu Type %d at TS = %lf RNGs:", lp->gid, m->type, tw_now(lp) );
    rng_write_state( lp->rng, olsr_event_log );
#endif

#if ENABLE_OPTIMISTIC
    if( g_tw_synchronization_protocol == OPTIMISTIC )
    {
        memcpy( &(m->state_copy), s, sizeof(node_state));
    }
#endif

    g_olsr_event_stats[m->type]++;

    switch (m->type) {
        case SA_MASTER_TX:
            //printf("RECEIVED SA_MASTER_TX in ERROR\n");
            //fflush(stdout);
            break;

        case SA_MASTER_RX:
            //printf("RECEIVED SA_MASTER_RX VALIDLY\n");
            //fflush(stdout);

            x = log((double)(nlp_per_pe - SA_range_start) * tw_nnodes());

#if DEBUG
            printf("x = log (%lf)\n", (double)(nlp_per_pe - SA_range_start) * tw_nnodes());

            printf("x1 is %lf\n", x);
#endif

            x = x / log(2.0);

#if DEBUG
            printf("x2 is %lf\n", x);

            printf("m->level is %d\n", m->level);
#endif

            //if (log2((nlp_per_pe - SA_range_start) * tw_nnodes()) > m->level) {
            if (x > m->level) {
                // Send a new SA_MASTER_RX to an SA Master
                ts = 1.0 + tw_rand_unif(&lp->cur_state->rng);
                dest = master_hierarchy(lp->gid, m->level+1);
#if DEBUG
                if (olsr_map(dest) != olsr_map(lp->gid)) {
                    printf("Sending a remote message from %llu to %llu: LP gid %llu to %llu\n",
                           olsr_map(lp->gid), olsr_map(dest), lp->gid, dest);
                }
#endif

                e = tw_event_new(dest, ts, lp);
                msg = (olsr_msg_data*)tw_event_data(e);
                msg->type = SA_MASTER_RX;
                msg->originator = s->get_local_address();
                msg->sender = s->get_local_address();
                msg->destination = dest;
                msg->level = m->level + 1;
                tw_event_send(e);
            }


//            for (i = 0; i < total_regions; i++) {
//                if (s->local_address == total_nodes + i) {
//                    printf("Don't send a satellite message to ourselves! %lu->%d\n",
//                           s->local_address, total_nodes + i);
//                    continue;
//                }
//                // 2 second round-trip for satellite communications
//                ts = 2.0 + tw_rand_unif(lp->rng);
//                e = tw_event_new(total_nodes + i, ts, lp);
//                msg = tw_event_data(e);
//                msg->type = SA_MASTER_RX;
//                tw_event_send(e);
//            }
            break;

        default:
            break;
    }
}

void olsr_event_reverse(node_state *s, tw_bf *bf, olsr_msg_data *m, tw_lp *lp)
{
#if ENABLE_OPTIMISTIC
    if( g_tw_synchronization_protocol == OPTIMISTIC )
      {
	memcpy( s, &(m->state_copy), sizeof(node_state));
      }
    g_olsr_event_stats[m->type]--;
#endif
}

void sa_master_event_reverse(node_state *s, tw_bf *bf, olsr_msg_data *m, tw_lp *lp)
{
#if ENABLE_OPTIMISTIC
    if( g_tw_synchronization_protocol == OPTIMISTIC )
    {
        memcpy( s, &(m->state_copy), sizeof(node_state));
    }
    g_olsr_event_stats[m->type]--;
#endif
}

void olsr_final(node_state *s, tw_lp *lp)
{
    int i;

    if( OLSR_NO_FINAL_OUTPUT )
      return;

    if (s->get_local_address() % OLSR_MAX_NEIGHBORS == 0) {
        for (i = 0; i < OLSR_MAX_NEIGHBORS; i++) {
            printf("node %llu had %d SA packets received.\n", s->get_local_address(), s->SA_per_node[i]);
        }
    }

    printf("node %llu contains %d neighbors\n", s->get_local_address(), s->get_num_neigh());
    printf("x: %f   \ty: %f\n", s->get_lng(), s->get_lat());
    for (i = 0; i < s->get_num_neigh(); i++) {
        printf("   neighbor[%d] is %llu\n", i, s->get_neighSet(i)->neighborMainAddr);
        printf("   Dy(%llu) is %d\n", s->get_neighSet(i)->neighborMainAddr,
               s->Dy(s->get_neighSet(i)->neighborMainAddr));
    }

    printf("node %llu has %d two-hop neighbors\n", s->get_local_address(),
           s->get_num_two_hop());
    for (i = 0; i < s->get_num_two_hop(); i++) {
        printf("   two-hop neighbor[%d] is %llu : %llu\n", i,
               s->get_twoHopSet(i)->neighborMainAddr,
               s->get_twoHopSet(i)->twoHopNeighborAddr);
    }

    printf("node %llu has %d MPRs\n", s->get_local_address(),
           s->get_num_mpr());
    for (i = 0; i < s->get_num_mpr(); i++) {
        printf("   MPR[%d] is %llu\n", i,
               s->get_MprSet(i));
    }

    printf("node %llu had %d MPR selectors\n", s->get_local_address(), s->get_num_mpr_sel());

    printf("node %llu routing table\n", s->get_local_address());
    for (i = 0; i < s->get_num_routes(); i++) {
        printf("   route[%d]: dest: %llu \t next %llu \t distance %d\n",
               i, s->get_route_table(i)->destAddr,
               s->get_route_table(i)->nextAddr, s->get_route_table(i)->distance);
    }

    printf("node %llu top tuples\n", s->get_local_address());
    for (i = 0; i < s->get_num_top_set(); i++) {
        printf("   top_tuple[%d] dest: %llu   last:  %llu   seq:   %d\n",
               i, s->get_topSet(i)->destAddr, s->get_topSet(i)->lastAddr, s->get_topSet(i)->sequenceNumber);
    }

    /*
    for (i = 0; i < OLSR_MAX_NEIGHBORS; i++) {
        if (sqrt((s->lng - g_X[i]) * (s->lng - g_X[i]) +
                 (s->lat - g_Y[i]) * (s->lat - g_Y[i])) > RANGE) {
            printf("%lu and %d are out of range.\n", s->local_address, i);
        }
    }
    */
    printf("\n");
}

extern unsigned int nkp_per_pe;

tw_peid olsr_map(tw_lpid gid)
{
    if (gid < SA_range_start * tw_nnodes()) {
        return (tw_peid)gid / SA_range_start;
    }
    // gid is above the max lpid, it must be an aggregator
    gid -= SA_range_start * tw_nnodes();
    gid /= (SA_range_start / OLSR_MAX_NEIGHBORS);
    return gid;
}

//#define VERIFY_MAPPING 1

void olsr_custom_mapping(void)
{
    tw_pe	*pe;

	int	 nlp_per_kp;
	int	 lpid;
	int	 kpid;
	int	 i;
	int	 j;

    int foo;

	// may end up wasting last KP, but guaranteed each KP has == nLPs
	nlp_per_kp = ceil((double) g_tw_nlp / (double) g_tw_nkp);

	if(!nlp_per_kp)
		tw_error(TW_LOC, "Not enough KPs defined: %d", g_tw_nkp);

	g_tw_lp_offset = g_tw_mynode * SA_range_start;
    foo = g_tw_lp_offset;

#if VERIFY_MAPPING
	printf("NODE %d: nlp %lld, offset %lld\n",
           g_tw_mynode, g_tw_nlp, g_tw_lp_offset);
#endif

	for(kpid = 0, lpid = 0, pe = nullptr; (pe = tw_pe_next(pe)); )
	{
#if VERIFY_MAPPING
		printf("\tPE %d\n", pe->id);
#endif

		for(i = 0; i < nkp_per_pe; i++, kpid++)
		{
			tw_kp_onpe(kpid, pe);

#if VERIFY_MAPPING
			printf("\t\tKP %d", kpid);
#endif


			for(j = 0; j < nlp_per_kp && lpid < g_tw_nlp; j++, lpid++)
			{

                if (lpid < SA_range_start) {
                    tw_lp_onpe(lpid, pe, g_tw_lp_offset+lpid);
                    tw_lp_onkp(g_tw_lp[lpid], g_tw_kp[kpid]);
                }

                else {
#if VERIFY_MAPPING
                    printf("mapping LP %d to gid %d on PE %llu\n", lpid, SA_range_start * tw_nnodes() + region(foo), pe->id);
#endif
                    tw_lp_onpe(lpid, pe, SA_range_start * tw_nnodes() + region(foo));
                    foo += OLSR_MAX_NEIGHBORS;
                    tw_lp_onkp(g_tw_lp[lpid], g_tw_kp[kpid]);
                }

#if VERIFY_MAPPING
				if(0 == j % 20)
					printf("\n\t\t\t");
				printf("%lld ", lpid+g_tw_lp_offset);
#endif
			}

#if VERIFY_MAPPING
			printf("\n");
#endif
		}
	}

	if(!g_tw_lp[g_tw_nlp-1])
		tw_error(TW_LOC, "Not all LPs defined! (g_tw_nlp=%d)", g_tw_nlp);

    /*
	if(g_tw_lp[g_tw_nlp-1]->gid != g_tw_lp_offset + g_tw_nlp - 1)
		tw_error(TW_LOC, "LPs not sequentially enumerated!");
     */
}

tw_lp * olsr_mapping_to_lp(tw_lpid lpid)
{
    assert(lpid >= 0);
    assert(lpid < g_tw_nlp * tw_nnodes());

    int id = lpid;

    if (id >= SA_range_start * tw_nnodes()) {
        id -= SA_range_start * tw_nnodes();
        id %= SA_range_start / OLSR_MAX_NEIGHBORS;
        id += SA_range_start;

#if VERIFY_MAPPING
        printf("Accessing gid %lu -> g_tw_lp[%d]\n", lpid, id);
#endif

        assert(id < g_tw_nlp);
        return g_tw_lp[id];
    }

    id %= SA_range_start;

#if VERIFY_MAPPING
    printf("Accessing gid %lu -> g_tw_lp[%d]\n", lpid, id);
#endif

    assert(id < g_tw_nlp);
    return g_tw_lp[id];
}

void null(void)
{

}

node_state ns;
node_state sa_ns;

tw_lptype olsr_lps[] = {
    // Our OLSR node handling functions
    {
        (init_f) olsr_init,
        (pre_run_f) nullptr,
        (event_f) olsr_event,
        (revent_f) olsr_event_reverse,
        (final_f) olsr_final,
        (map_f) olsr_map,
        sizeof(node_state),
        &ns
    },
    // Our SA aggregator handling functions
    {
        (init_f) sa_master_init,
        (pre_run_f) nullptr,
        (event_f) sa_master_event,
        (revent_f) sa_master_event_reverse,
        (final_f) null,
        (map_f) olsr_map,
        sizeof(node_state),
        &sa_ns
    },
    { 0 },
};


