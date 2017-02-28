/*******************************************************************************
********************************************************************************

    digraphop : a set of operations on directed graphes

    Created by Yann Orlarey on 31/01/2017.
    Copyright © 2017 Grame. All rights reserved.

 *******************************************************************************
 ******************************************************************************/

#ifndef digraphop_hpp
#define digraphop_hpp

#include <iostream>
#include <list>
#include <vector>

#include "digraph.hpp"

using namespace std;

//===========================================================
//===========================================================
// Partitions a graph into strongly connected components
// USAGE : Tarjan<N> t(mygraph); ...t.partition()...
//===========================================================
//===========================================================

template <typename N>
class Tarjan
{
    // Additional information associated to each node
    // during the visit
    struct tarjanAux {
        bool fVisited = false;
        bool fStacked = false;
        int  fNum1    = 0;
        int  fNum2    = 0;
    };

    const digraph<N>& fGraph;
    int               fGroup;
    stack<N>          fStack;
    map<N, tarjanAux> fAux;
    set<set<N>> fPartition;
    int         fCycleCount;

    // visit a specific node n of the graph
    void visit(const N& v)
    {
        // cout << "start (first) visit of " << v << endl;
        auto& x = fAux[v];
        fStack.push(v);
        x.fStacked = true;
        x.fVisited = true;
        x.fNum1 = x.fNum2 = fGroup;
        ++fGroup;

        // cout << "visit all nodes connected to " << v << endl;
        for (const auto& p : fGraph.connections(v)) {
            // cout << "we have a connection " << v << "-" << p.second << "->" << p.first << endl;
            const N& w = p.first;
            auto&    y = fAux[w];
            if (!y.fVisited) {
                visit(w);
                x.fNum2 = min(x.fNum2, y.fNum2);
            } else {
                if (y.fStacked) {
                    // cout << "the node " << w << " is already in the stack" << endl;
                    x.fNum2 = min(x.fNum2, y.fNum1);
                }
            }
        }

        if (x.fNum1 == x.fNum2) {
            // cout << "the node " << v << " is the root of a cycle" << endl;

            set<N> cycle;
            bool   finished = false;
            do {
                const N& w = fStack.top();
                fStack.pop();
                fAux[w].fStacked = false;
                cycle.insert(w);
                finished = (w == v);
            } while (!finished);
            fPartition.insert(cycle);
            if ((cycle.size() > 1) || fGraph.areConnected(v, v)) {
                fCycleCount++;
            }
        }
        // cout << "end (first) visit of " << v << endl;
    }

   public:
    Tarjan(const digraph<N>& g) : fGraph(g), fGroup(0), fCycleCount(0)
    {
        for (const auto& n : fGraph.nodes()) {
            if (fAux.find(n) == fAux.end()) {
                visit(n);
            }
        }
    }

    const set<set<N>>& partition() const
    {
        return fPartition;
    }

    int cycles() const
    {
        return fCycleCount;
    }
};

//===========================================================
//===========================================================
// cycles:graph->int : counts the number of cycles
// (strongly connected components) of a graph
//===========================================================
//===========================================================

template <typename N>
inline int cycles(const digraph<N>& g)
{
    Tarjan<N> T(g);
    return T.cycles();
}

//===========================================================
//===========================================================
// graph2dag : transfoms a graph into a dag of supernodes, ie
// strongly connected components.
//===========================================================
//===========================================================

template <typename N>
inline digraph<digraph<N>> graph2dag(const digraph<N>& g)
{
    Tarjan<N> T(g);
    map<N, digraph<N>> M;
    digraph<digraph<N>> sg;

    // build the graph of supernodes
    for (const auto& s : T.partition()) {
        digraph<N> sn;
        for (const N& n : s) {
            M.insert(make_pair(n, sn));
            sn.add(n);
        }
        sg.add(sn);
    }

    // compute the connections between the supernodes
    for (const auto& n1 : g.nodes()) {
        digraph<N> sn1(M[n1]);
        for (const auto& c : g.connections(n1)) {
            digraph<N> sn2(M[c.first]);
            if (sn1 == sn2) {
                // the connection is inside the same supernode
                sn1.add(n1, c.first, c.second);
            } else {
                // the connection is between supernodes
                sg.add(sn1, sn2, c.second);
            }
        }
    }

    return sg;
}

//===========================================================
//===========================================================
//
// parallelize : transfoms a DAG into a sequential vector
// of parallel vectors of nodes using a topological sort.
//
//===========================================================
//===========================================================

template <typename N>
inline vector<vector<N>> parallelize(const digraph<N>& g)
{
    //-----------------------------------------------------------
    // Find the level of a node n -> {m1,m2,...} such that
    //		level(n -> {})			= 0
    //		level(n -> {m1,m2,...})	= 1 + max(level(mi))
    //-----------------------------------------------------------
    typedef function<int(const digraph<N>& g, const N& n1, map<N, int>&)> Levelfun;

    Levelfun level = [&level](const digraph<N>& g, const N& n1, map<N, int>& levelcache) -> int {
        auto p = levelcache.find(n1);
        if (p != levelcache.end()) {
            return p->second;
        } else {
            int l = -1;
            for (const auto& e : g.connections(n1)) {
                l = max(l, level(g, e.first, levelcache));
            }
            return levelcache[n1] = l + 1;
        }
    };

    map<N, int> levelcache;
    // compute the level of each node in the graph
    int l = -1;
    for (const N& n : g.nodes()) {
        l = max(l, level(g, n, levelcache));
    }
    // create a graph for each level and place
    // each node in the appropriate level
    vector<vector<N>> v;
    v.resize(l + 1);
    for (const N& n : g.nodes()) {
        v[levelcache[n]].push_back(n);
    }

    return v;
}

//===========================================================
//===========================================================
// serialize : transfoms a DAG into a sequence of nodes
// using a topological sort.
//===========================================================
//===========================================================

template <typename N>
inline vector<N> serialize(const digraph<N>& g)
{
    //------------------------------------------------------------------------
    // visit : a local function (simulated using a lambda) to visit a graph
    // g : the graph
    // n : the node
    // V : set of already visited nodes
    // S : serialized vector of nodes
    //------------------------------------------------------------------------
    typedef function<void(const digraph<N>& g, const N& n, set<N>& V, vector<N>& S)> Visitfun;
    Visitfun visit = [&visit](const digraph<N>& g, const N& n, set<N>& V, vector<N>& S) {
        if (V.find(n) == V.end()) {
            V.insert(n);
            for (const auto& p : g.connections(n)) {
                visit(g, p.first, V, S);
            }
            S.push_back(n);
        }
    };

    vector<N> S;
    set<N>    V;
    for (const N& n : g.nodes()) {
        visit(g, n, V, S);
    }
    return S;
}

//===========================================================
//===========================================================
// mapgraph(foo) : transfoms a graph  by applying foo:N->M
// to each node of graph. The connections are preserved.
//===========================================================
//===========================================================

template <typename N, typename M>
inline digraph<M> mapnodes(const digraph<N>& g, function<M(const N&)> foo)
{
    digraph<M> r;
    map<N, M> cache;
    // create a new graph with the transformed nodes
    for (const auto& n1 : g.nodes()) {
        M n2 = foo(n1);
        r.add(n2);
        cache.insert(make_pair(n1, n2));
    }

    // copy the connections
    for (const auto& n : g.nodes()) {
        for (const auto& cnx : g.connections(n)) {
            r.add(cache[n], cache[cnx.first], cnx.second);
        }
    }
    return r;
}

//===========================================================
//===========================================================
// mapconnections(g, keep) -> g' : transfoms a graph by
// applying the function keep to each connection. If keep
// returns true the connection is maintained, otherwise it
// is removed.
//===========================================================
//===========================================================

template <typename N>
inline digraph<N> mapconnections(const digraph<N>& G, function<bool(const N&, const N&, int)> keep)
{
    digraph<N> R;
    for (const N& n : G.nodes()) {
        R.add(n);
        for (const auto& c : G.connections(n)) {
            if (keep(n, c.first, c.second)) {
                R.add(n, c.first, c.second);
            }
        }
    }
    return R;
}

//===========================================================
//===========================================================
// cut(g,d) -> g'
// cuts all the connections of graph g of weight >= d
//===========================================================
//===========================================================

template <typename N>
inline digraph<N> cut(const digraph<N>& G, int dm)
{
    return mapconnections<N>(G, [dm](const N& n1, const N& n2, int d) -> bool { return d < dm; });
}

/*******************************************************************************
********************************************************************************

                                        VARIOUS PRINTING FUNCTIONS

 *******************************************************************************
 ******************************************************************************/

//===========================================================
//===========================================================
// file << graph : print graph on a stream
//===========================================================
//===========================================================

template <typename N>
inline ostream& operator<<(ostream& file, const digraph<N>& g)
{
    string sep      = "";
    bool   hasnodes = false;

    file << "Graph {";
    for (const N& n : g.nodes()) {
        hasnodes    = true;
        bool hascnx = false;
        for (const auto& c : g.connections(n)) {
            hascnx = true;
            if (c.second == 0) {
                file << sep << n << "->" << (c.first);
            } else {
                file << sep << n << '-' << c.second << "->" << (c.first);
            }
            sep = ", ";
        }
        if (!hascnx) {
            file << sep << n;
        }
        sep = ", ";
    }

    return file << "}";
}

//===========================================================
//===========================================================
// file << list : print a list on a stream
//===========================================================
//===========================================================

template <typename N>
inline ostream& operator<<(ostream& file, const list<N>& L)
{
    string sep = "";

    file << "list {";
    for (const N& e : L) {
        file << sep << e;
        sep = ", ";
    }
    return file << "}";
}

//===========================================================
//===========================================================
// file << vector : print a vector on a stream
//===========================================================
//===========================================================

template <typename N>
inline ostream& operator<<(ostream& file, const vector<N>& V)
{
    string sep = "";

    file << "vector {";
    for (const N& e : V) {
        file << sep << e;
        sep = ", ";
    }
    return file << "}";
}

//===========================================================
//===========================================================
// file << pair : print a pair on a stream
//===========================================================
//===========================================================

template <typename N, typename M>
inline ostream& operator<<(ostream& file, const pair<N, M>& V)
{
    return file << "pair {" << V.first << ", " << V.second << "}";
}

#endif /* digraphop_hpp */
