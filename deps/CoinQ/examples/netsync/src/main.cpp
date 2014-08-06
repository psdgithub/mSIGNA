///////////////////////////////////////////////////////////////////////////////
//
// netsync example program 
//
// main.cpp
//
// Copyright (c) 2012-2014 Eric Lombrozo
//
// All Rights Reserved.

#include <CoinCore/CoinNodeData.h>
#include <CoinCore/MerkleTree.h>
#include <CoinCore/BloomFilter.h>
#include <CoinCore/typedefs.h>
#include <CoinCore/random.h>

#include <CoinQ/CoinQ_netsync.h>
#include <CoinQ/CoinQ_coinparams.h>

#include <stdutils/stringutils.h>

#include <logger/logger.h>

#include <signal.h>

bool g_bShutdown = false;

void finish(int sig)
{
    std::cout << "Stopping..." << std::endl;
    g_bShutdown = true;
}

using namespace CoinQ;
using namespace Coin;
using namespace std;

int main(int argc, char* argv[])
{
    try
    {
        NetworkSelector networkSelector;

        if (argc < 3)
        {
            cerr << "# Usage: " << argv[0] << " <network> <host> [port] [bloom filter elements = 10]" << endl
                 << "# Supported networks: " << stdutils::delimited_list(networkSelector.getNetworkNames(), ", ") << endl;
            return -1;
        }

        cout << endl << "Initializing logger to file netsync.log..." << endl;
        INIT_LOGGER("netsync.log");

        CoinParams coinParams = networkSelector.getCoinParams(argv[1]);
        string host = argv[2];
        string port = (argc > 3) ? argv[3] : coinParams.default_port();
        unsigned int filterElements = (argc > 4) ? strtoul(argv[4], NULL, 0) : 10;

        cout << endl << "Connecting to " << coinParams.network_name() << " peer" << endl
             << "-------------------------------------------" << endl
             << "  host:             " << host << endl
             << "  port:             " << port << endl
             << "  magic bytes:      " << hex << coinParams.magic_bytes() << endl
             << "  protocol version: " << dec << coinParams.protocol_version() << endl
             << endl;

        unsigned int blockTxIndex = 0;

        Network::NetworkSync networkSync(coinParams);
        networkSync.loadHeaders("blocktree.dat", false, [&](const CoinQBlockTreeMem& blocktree) {
            cout << "Best height: " << blocktree.getBestHeight() << " Total work: " << blocktree.getTotalWork().getDec() << endl;
        });

        networkSync.subscribeStarted([&]() {
            cout << "NetworkSync started." << endl;
        });

        networkSync.subscribeStopped([&]() {
            cout << "NetworkSync stopped." << endl;
        });

        networkSync.subscribeOpen([&]() {
            cout << "NetworkSync open." << endl;
        });

        networkSync.subscribeClose([&]() {
            cout << "NetworkSync closed." << endl;
        });

        networkSync.subscribeTimeout([&]() {
            cout << "NetworkSync timeout." << endl;
        });

        networkSync.subscribeConnectionError([&](const string& error) {
            cout << "NetworkSync connection error: " << error << endl;
        });

        networkSync.subscribeProtocolError([&](const string& error) {
            cout << "NetworkSync protocol error: " << error << endl;
        });

        networkSync.subscribeBlockTreeError([&](const string& error) {
            cout << "NetworkSync block tree error: " << error << endl;
        });

        networkSync.subscribeFetchingHeaders([&]() {
            cout << "NetworkSync fetching headers." << endl;
        });

        networkSync.subscribeHeadersSynched([&]() {
            cout << "NetworkSync headers synched." << endl;
            hashvector_t hashes;
            networkSync.syncBlocks(hashes, time(NULL) - 10*60*60); // Start 10 hours earlier
        });

        networkSync.subscribeFetchingBlocks([&]() {
            cout << "NetworkSync fetching blocks." << endl;
        });

        networkSync.subscribeBlocksSynched([&]() {
            cout << "NetworkSync blocks synched." << endl;
        });

        networkSync.subscribeStatus([&](const string& status) {
            cout << "NetworkSync status: " << status << endl;
        });

        networkSync.subscribeNewTx([&](const Transaction& tx) {
            cout << endl << "NEW TX: " << tx.getHashLittleEndian().getHex() << endl;
        });

        networkSync.subscribeMerkleTx([&](const ChainMerkleBlock& merkleBlock, const Transaction& tx, unsigned int txIndex, unsigned int txTotal)
        {
            cout << "  tx (" << txIndex << "/" << (txTotal - 1) << "): " << tx.getHashLittleEndian().getHex() << endl;
        });

        networkSync.subscribeBlock([&](const ChainBlock& block) {
            cout << "NEW BLOCK: " << block.blockHeader.getHashLittleEndian().getHex() << " height: " << block.height << endl;
        });

        networkSync.subscribeMerkleBlock([&](const ChainMerkleBlock& merkleblock) {
            cout << endl << "NEW MERKLE BLOCK" << endl
                         << "  hash: " << merkleblock.blockHeader.getHashLittleEndian().getHex() << endl
                         << "  height: " << merkleblock.height << endl;

            try
            {
                PartialMerkleTree tree(merkleblock.nTxs, merkleblock.hashes, merkleblock.flags, merkleblock.blockHeader.merkleRoot);
                std::vector<uchar_vector> txhashes = tree.getTxHashesLittleEndianVector();
                unsigned int i = 0;
                cout << "should contain txs:" << endl;
                for (auto& txhash: txhashes) { cout << "  tx " << i++ << ": " << uchar_vector(txhash).getHex() << endl; }
            }
            catch (const exception& e)
            {
                cout << "Error constructing partial merkle tree: " << e.what() << endl;
            }

            cout << "--------------------" << endl;
            blockTxIndex = 0;
        });

        networkSync.subscribeAddBestChain([&](const ChainHeader& header) {
            cout << "NetworkSync added to best chain: " << header.getHashLittleEndian().getHex() << " height: " << header.height << endl;
        });

        networkSync.subscribeRemoveBestChain([&](const ChainHeader& header) {
            cout << "NetworkSync removed from best chain: " << header.getHashLittleEndian().getHex() << " height: " << header.height << endl;
        });

        networkSync.subscribeBlockTreeChanged([&]() {
            cout << "NetworkSync block tree changed." << endl;
        });

        // Set the bloom filter
        cout << endl << "Bloom filter settings" << endl
                     << "---------------------" << endl
                     << "  elements:            " << filterElements << endl
                     << "  false positive rate: 0.001" << endl
                     << "  nTweak:              0" << endl
                     << "  nFlags:              0" << endl;

        BloomFilter filter(filterElements, 0.001, 0, 0);
        for (unsigned int i = 0; i < filterElements; i++) { filter.insert(random_bytes(32)); }
        networkSync.setBloomFilter(filter);

        cout << endl << "Registering SIGINT and SIGTERM signal handlers.." << endl;
        signal(SIGINT, &finish);
        signal(SIGTERM, &finish);

        LOGGER(info) << endl << endl << endl << endl << endl << endl;
        cout << endl << "Starting sync..." << endl;
        LOGGER(info) << "Starting..." << endl;
        networkSync.start(host, port);
        while (!g_bShutdown) { usleep(200); }

        cout << "Stopping..." << endl;
        LOGGER(info) << "Stopping..." << endl;
        networkSync.stop();

        cout << "Stopped." << endl;
        LOGGER(info) << "Stopped." << endl;
    }
    catch (const exception& e)
    {
        cerr << "Unexpected termination. Error: " << e.what() << endl;
        LOGGER(error) << "Unexpected termination. Error: " << e.what() << endl;
        return -2;
    }

    return 0;
}
