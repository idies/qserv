#include <stdio.h>
#include <algorithm>
#include <iostream>

#include "boost/ref.hpp"
#include "boost/thread.hpp"
#include "boost/timer/timer.hpp"

#include "Block.h"
#include "Geometry.h"
#include "HtmIndex.h"
#include "Merger.h"
#include "Options.h"

using std::cerr;
using std::cout;
using std::endl;
using std::flush;
using std::max;

using boost::timer::cpu_timer;

using namespace dupr;


namespace {

struct State {
    State(Options const & options, InputBlockVector const & blocks);
    ~State();

    void operator()();

    Options const &  options;  // Indexing options

    char             cl0[CACHE_LINE_SIZE];

    boost::mutex     mutex;
    InputBlockVector blocks;   // Input blocks
    Merger           merger;   // Block merger
    HtmIndex         htmIndex;

    char             cl1[CACHE_LINE_SIZE];
};

State::State(Options const & options, InputBlockVector const & blocks) :
    options(options),
    mutex(),
    blocks(blocks),
    merger(options.indexDir + "/data.csv",
           options.indexDir + "/ids.bin",
           options.scratchDir + "/scratch.bin",
           options.blockSize,
           options.k,
           blocks.size()),
    htmIndex(options.htmLevel)
{ }

State::~State() { }

/* The processing loop for threads. Note that this scheme can be improved on.
   In particular, it would be better to adjust the number of threads that are
   reading blocks separately from the number of blocks that are processing
   blocks. As it stands, saturating IO/CPU will result in over/under
   subscription of CPU/IO, unless the IO rate closely matches the processing
   rate.
 */
void State::operator()() {
    try {
        while (true) {
            boost::shared_ptr<InputBlock> block;
            // get a block to process
            {
                boost::lock_guard<boost::mutex> lock(mutex);
                if (blocks.empty()) {
                    break; // none left
                }
                block = blocks.back();
                blocks.pop_back();
            }
            // read the block
            block->read();
            // process the block
            block->process(options, mutex, htmIndex);
            // add the block to the merge queue
            merger.add(block);
        }
    } catch (std::exception const & ex) {
        cerr << ex.what() << endl;
        exit(EXIT_FAILURE);
    }
}


void index(Options const & options) {
    int const numThreads = max(1, options.numThreads);
    cout << "Initializing... " << endl;
    cpu_timer t;
    State state(options, splitInputs(options.inputFiles, options.blockSize));
    t.stop();
    cout << "\tsplit inputs into " << state.blocks.size() << " blocks : " << t.format();
    cout << "Indexing input... " << endl;
    cpu_timer t2;
    // create thread pool
    boost::scoped_array<boost::thread> threads(new boost::thread[numThreads - 1]);
    for (int t = 0; t < numThreads - 1; ++t) {
        threads[t] = boost::thread(boost::ref(state));
    }
    // the calling thread participates in processing
    state();
    // wait for all threads to finish
    for (int t = 0; t < numThreads - 1; ++t) {
        threads[t].join();
    }
    t2.stop();
    cout << "\tfirst pass finished : " << t2.format() << flush;
    cpu_timer t3;
    // Finish up the merge
    state.merger.finish();
    t3.stop();
    cout << "\tmerging finished    : " << t3.format() << flush;
    // Write the HTM index
    state.htmIndex.write(options.indexDir + "/map.bin");
}

} // unnamed namespace


int main(int argc, char ** argv) {
    try {
        cpu_timer total;
        Options options = parseIndexerCommandLine(argc, argv);
        index(options);
        cout << endl << "Indexer finished : " << total.format() << endl;
    } catch (std::exception const & ex) {
        cerr << ex.what() << endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

