/**********************************
 * FILE NAME: MP1Node.cpp
 *
 * DESCRIPTION: Membership protocol run by this Node.
 * 				Definition of MP1Node class functions. (Revised 2020)
 *
 *  Starter code template
 **********************************/

#include "MP1Node.h"

/*
 * Note: You can change/add any functions in MP1Node.{h,cpp}
 */

/**
 * Overloaded Constructor of the MP1Node class
 * You can add new members to the class if you think it
 * is necessary for your logic to work
 */
MP1Node::MP1Node(Params *params, EmulNet *emul, Log *log, Address *address)
{
    for (int i = 0; i < 6; i++)
    {
        NULLADDR[i] = 0;
    }
    this->memberNode = new Member;
    this->shouldDeleteMember = true;
    memberNode->inited = false;
    this->emulNet = emul;
    this->log = log;
    this->par = params;
    this->memberNode->addr = *address;
}

/**
 * Overloaded Constructor of the MP1Node class
 * You can add new members to the class if you think it
 * is necessary for your logic to work
 */
MP1Node::MP1Node(Member *member, Params *params, EmulNet *emul, Log *log, Address *address)
{
    for (int i = 0; i < 6; i++)
    {
        NULLADDR[i] = 0;
    }
    this->memberNode = member;
    this->shouldDeleteMember = false;
    this->emulNet = emul;
    this->log = log;
    this->par = params;
    this->memberNode->addr = *address;
}

/**
 * Destructor of the MP1Node class
 */
MP1Node::~MP1Node()
{
    if (shouldDeleteMember)
    {
        delete this->memberNode;
    }
}

/**
 * FUNCTION NAME: recvLoop
 *
 * DESCRIPTION: This function receives message from the network and pushes into the queue
 *                 This function is called by a node to receive messages currently waiting for it
 */
int MP1Node::recvLoop()
{
    if (memberNode->bFailed)
    {
        return false;
    }
    else
    {
        return emulNet->ENrecv(&(memberNode->addr), enqueueWrapper, NULL, 1, &(memberNode->mp1q));
    }
}

/**
 * FUNCTION NAME: enqueueWrapper
 *
 * DESCRIPTION: Enqueue the message from Emulnet into the queue
 */
int MP1Node::enqueueWrapper(void *env, char *buff, int size)
{
    Queue q;
    return q.enqueue((queue<q_elt> *)env, (void *)buff, size);
}

/**
 * FUNCTION NAME: nodeStart
 *
 * DESCRIPTION: This function bootstraps the node
 *                 All initializations routines for a member.
 *                 Called by the application layer.
 */
void MP1Node::nodeStart(char *servaddrstr, short servport)
{
    Address joinaddr;
    joinaddr = getJoinAddress();

    // Self booting routines
    if (initThisNode(&joinaddr) == -1)
    {
#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "init_thisnode failed. Exit.");
#endif
        exit(1);
    }

    if (!introduceSelfToGroup(&joinaddr))
    {
        finishUpThisNode();
#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "Unable to join self to group. Exiting.");
#endif
        exit(1);
    }

    return;
}

/**
 * FUNCTION NAME: initThisNode
 *
 * DESCRIPTION: Find out who I am and start up
 */
int MP1Node::initThisNode(Address *joinaddr)
{
    /*
     * This function is partially implemented and may require changes
     */
    int id = *(int *)(&memberNode->addr.addr);
    int port = *(short *)(&memberNode->addr.addr[4]);

    memberNode->bFailed = false;
    memberNode->inited = true;
    memberNode->inGroup = false;
    // node is up!
    memberNode->nnb = 0;
    memberNode->heartbeat = 0;
    memberNode->pingCounter = TFAIL;
    memberNode->timeOutCounter = -1;
    initMemberListTable(memberNode);

    return 0;
}

/**
 * FUNCTION NAME: introduceSelfToGroup
 *
 * DESCRIPTION: Join the distributed system
 */
int MP1Node::introduceSelfToGroup(Address *joinaddr)
{
    MessageHdr *msg;
#ifdef DEBUGLOG
    static char s[1024];
#endif

    if (0 == strcmp((char *)&(memberNode->addr.addr), (char *)&(joinaddr->addr)))
    {
        // I am the group booter (first process to join the group). Boot up the group
#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "Starting up group...");
#endif
        memberNode->inGroup = true;
        addToMemberList(*joinaddr, memberNode->heartbeat);
    }
    else
    {
        size_t msgsize = sizeof(MessageHdr) + sizeof(joinaddr->addr) + sizeof(long) + 1;
        msg = (MessageHdr *)malloc(msgsize * sizeof(char));

        // create JOINREQ message: format of data is {struct Address myaddr}
        msg->msgType = JOINREQ;
        memcpy((char *)(msg + 1), &memberNode->addr.addr, sizeof(memberNode->addr.addr));
        memcpy((char *)(msg + 1) + 1 + sizeof(memberNode->addr.addr), &memberNode->heartbeat, sizeof(long));

#ifdef DEBUGLOG
        sprintf(s, "Trying to join...");
        log->LOG(&memberNode->addr, s);
#endif

        // send JOINREQ message to introducer member
        emulNet->ENsend(&memberNode->addr, joinaddr, (char *)msg, msgsize);

        free(msg);
    }

    return 1;
}

/**
 * FUNCTION NAME: finishUpThisNode
 *
 * DESCRIPTION: Wind up this node and clean up state
 */
int MP1Node::finishUpThisNode()
{
    memberNode->memberList.clear();
    memberNode->inGroup = false;
    return 0;
}

/**
 * FUNCTION NAME: nodeLoop
 *
 * DESCRIPTION: Executed periodically at each member
 *                 Check your messages in queue and perform membership protocol duties
 */
void MP1Node::nodeLoop()
{
    if (memberNode->bFailed)
    {
        return;
    }

    // Check my messages
    checkMessages();

    // Wait until you're in the group...
    if (!memberNode->inGroup)
    {
        return;
    }

    // ...then jump in and share your responsibilites!
    nodeLoopOps();

    return;
}

/**
 * FUNCTION NAME: checkMessages
 *
 * DESCRIPTION: Check messages in the queue and call the respective message handler
 */
void MP1Node::checkMessages()
{
    void *ptr;
    int size;

    // Pop waiting messages from memberNode's mp1q
    while (!memberNode->mp1q.empty())
    {
        ptr = memberNode->mp1q.front().elt;
        size = memberNode->mp1q.front().size;
        memberNode->mp1q.pop();
        recvCallBack((void *)memberNode, (char *)ptr, size);
    }
    return;
}

/**
 * FUNCTION NAME: recvCallBack
 *
 * DESCRIPTION: Message handler for different message types
 */
bool MP1Node::recvCallBack(void *env, char *data, int size)
{
    // TODO:encapsulate
    MessageHdr *msg = (MessageHdr *)data;

#ifdef DEBUGLOG
    static char s[1024];
#endif

    if (msg->msgType == JOINREQ)
    {
        Address sender;
        long heartbeat;

        // 1. Read addr and hearbeat message, skip header
        memcpy(sender.addr, (char *)(msg + 1), sizeof(memberNode->addr.addr));
        memcpy(&heartbeat, (char *)(msg + 1) + 1 + sizeof(memberNode->addr.addr), sizeof(long));

        // 2. Add to member list
        addToMemberList(sender, heartbeat);

#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "Read and added entry to member list");
#endif
        // process adds member and needs to log it
        log->logNodeAdd(&memberNode->addr, &sender);

        // 3. Respond with JOINREP

        // decide of structure
        // [Header][MemberListEntry * Size of member list (N)] - chose simplest structure
        size_t memberListSize = sizeof(MemberListEntry) * memberNode->memberList.size();
        size_t msgsize = sizeof(MessageHdr) + memberListSize;
        MessageHdr *msgResponse = (MessageHdr *)malloc(msgsize * sizeof(char));

        // create JOINREP message: format of data is [Header][MemberListEntry * N]
        msgResponse->msgType = JOINREP;
        memcpy((char *)(msgResponse + 1), memberNode->memberList.data(), memberListSize);

#ifdef DEBUGLOG
        sprintf(s, "Sending JOINREP");
        log->LOG(&memberNode->addr, s);
#endif

        // send JOINREP message back to the joining node
        emulNet->ENsend(&memberNode->addr, &sender, (char *)msgResponse, msgsize);
        free(msgResponse);
    }
    else if (msg->msgType == JOINREP)
    {
        memberNode->inGroup = true;

#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "Joined group");
#endif

        int n = (size - sizeof(MessageHdr)) / sizeof(MemberListEntry);
        MemberListEntry *entriesRecv = (MemberListEntry *)(msg + 1); // skip header

        // adding every new member into list
        for (int i = 0; i < n; ++i)
        {
            // skip if member exists already to help reduce false positives
            bool exists = false;
            for (auto &myEntry : memberNode->memberList)
            {
                // find recv entry with entry in my membership
                if (myEntry.id == entriesRecv[i].id && myEntry.port == entriesRecv[i].port)
                {
                    exists = true;
                    break; // stop looking through list and exit
                }
            }

            if (exists)
                continue; // don't add this row to my membership list

            Address memberAddr = makeNewAddr(entriesRecv[i].id, entriesRecv[i].port);
            addToMemberList(memberAddr, entriesRecv[i].heartbeat);

            if (memberAddr != memberNode->addr)
            {
                // log joins for other nodes
                log->logNodeAdd(&memberNode->addr, &memberAddr);
            }
        }
    }
    else if (msg->msgType == GOSSIP)
    {
        int n = (size - sizeof(MessageHdr)) / sizeof(MemberListEntry);
        MemberListEntry *entriesRecv = (MemberListEntry *)(msg + 1); // skip header

        for (int i = 0; i < n; ++i)
        {
            // skip my own row in membership list I received
            if (isThisMe(entriesRecv[i]))
                continue;

            MemberListEntry *row = nullptr;
            for (auto &myEntry : memberNode->memberList)
            {
                // try to find recv entry with entry in my membership
                if (myEntry.id == entriesRecv[i].id && myEntry.port == entriesRecv[i].port)
                {
                    row = &myEntry;
                    break;
                }
            }

            // add to membership list if recv entry not in my membership list
            if (row == nullptr)
            {
                // new member in recv entries so create new entry
                Address memberAddr = makeNewAddr(entriesRecv[i].id, entriesRecv[i].port);
                addToMemberList(memberAddr, entriesRecv[i].heartbeat);

                // log new addition
                log->logNodeAdd(&memberNode->addr, &memberAddr);

#ifdef DEBUGLOG
                log->LOG(&memberNode->addr, "Add entry to member list");
#endif
            }
            else if (entriesRecv[i].heartbeat > row->heartbeat)
            {
                // replace my heartbeat for this row with the receiver and update time
                row->heartbeat = entriesRecv[i].heartbeat;
                row->timestamp = par->getcurrtime();
            }
            // otherwise no need for change
        }
    }

    free(msg);

    return true;
}

/**
 * FUNCTION NAME: nodeLoopOps
 *
 * DESCRIPTION: Check if any node hasn't responded within a timeout period and then delete
 *                 the nodes
 *                 Propagate your membership list
 */
void MP1Node::nodeLoopOps()
{
    memberNode->heartbeat++;
    auto entry = memberNode->memberList.begin();

    // remove failed nodes from propagation so that I'm not sending failed notes
    vector<MemberListEntry> currNodesAlive;

    while (entry != memberNode->memberList.end())
    {
        Address memberAddr = makeNewAddr(entry->id, entry->port);

        if (memberAddr == memberNode->addr)
        {
            // don't delete myself, update new heartbeat and time
            entry->heartbeat = memberNode->heartbeat;
            entry->timestamp = par->getcurrtime();
            currNodesAlive.push_back(*entry);
            ++entry;
            continue;
        }

        long timeSinceAlive = par->getcurrtime() - entry->timestamp;

        // must wait a t cleanup (TREMOVE in this case) after TFAIL to delete row
        if (timeSinceAlive > TFAIL + TREMOVE)
        {
            // log removal
            log->logNodeRemove(&memberNode->addr, &memberAddr);
            entry = memberNode->memberList.erase(entry); // increments to next entry

#ifdef DEBUGLOG
            log->LOG(&memberNode->addr, "Deleted entry to member list");
#endif
        }
        else
        {
            if (timeSinceAlive <= TFAIL)
                currNodesAlive.push_back(*entry);
            ++entry;
        }
    }

    // propogate membership list with gossip -> sends its current alive membership list (currNodesAlive)
    // to a few of its neighbors
    size_t memberListSize = sizeof(MemberListEntry) * currNodesAlive.size();
    size_t msgsize = sizeof(MessageHdr) + memberListSize;
    MessageHdr *gossipMsg = (MessageHdr *)malloc(msgsize);

    gossipMsg->msgType = GOSSIP;
    memcpy((char *)(gossipMsg + 1), currNodesAlive.data(), memberListSize);

    for (int neighbor = 0; neighbor < NEIGHBORS; ++neighbor)
    {
        // randomize so no clustering
        int randomNeighbor = rand() % currNodesAlive.size();
        MemberListEntry &row = currNodesAlive[randomNeighbor];

        // skip if it generates me as the neighbor
        if (isThisMe(row))
            continue;

        Address neighborAddr = makeNewAddr(row.id, row.port);

        emulNet->ENsend(&memberNode->addr, &neighborAddr, (char *)gossipMsg, msgsize);
    }

    free(gossipMsg);

    return;
}

/**
 * FUNCTION NAME: isNullAddress
 *
 * DESCRIPTION: Function checks if the address is NULL
 */
int MP1Node::isNullAddress(Address *addr)
{
    return (memcmp(addr->addr, NULLADDR, 6) == 0 ? 1 : 0);
}

/**
 * FUNCTION NAME: getJoinAddress
 *
 * DESCRIPTION: Returns the Address of the coordinator
 */
Address MP1Node::getJoinAddress()
{
    Address joinaddr;

    *(int *)(&joinaddr.addr) = 1;
    *(short *)(&joinaddr.addr[4]) = 0;

    return joinaddr;
}

/**
 * FUNCTION NAME: initMemberListTable
 *
 * DESCRIPTION: Initialize the membership list
 */
void MP1Node::initMemberListTable(Member *memberNode)
{
    memberNode->memberList.clear();
}

/**
 * FUNCTION NAME: printAddress
 *
 * DESCRIPTION: Print the Address
 */
void MP1Node::printAddress(Address *addr)
{
    printf("%d.%d.%d.%d:%d \n", addr->addr[0], addr->addr[1], addr->addr[2],
           addr->addr[3], *(short *)&addr->addr[4]);
}

Address MP1Node::makeNewAddr(int id, short port)
{
    Address a;
    // addresses has same size for id and port
    memcpy(&a.addr, &id, sizeof(int));
    memcpy(&a.addr[4], &port, sizeof(short));
    return a;
}

void MP1Node::addToMemberList(const Address &a, long heartbeat)
{
    MemberListEntry e;
    int id = *(int *)(&a.addr);
    short port = *(short *)(&a.addr[4]);

    e.id = id;
    e.port = port;
    e.heartbeat = heartbeat;
    e.timestamp = par->getcurrtime();

    memberNode->memberList.push_back(e);
}

bool MP1Node::isThisMe(const MemberListEntry &e)
{
    return e.id == *(int *)(&memberNode->addr.addr[0]) && e.port == *(short *)(&memberNode->addr.addr[4]);
}