/****
DIAMOND protein aligner
Copyright (C) 2013-2017 Benjamin Buchfink <buchfink@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
****/

#include <numeric>
#include <utility>
#include <atomic>
#include "frequent_seeds.h"
#include "queries.h"
#include "../util/parallel/thread_pool.h"

using std::atomic;

const double Frequent_seeds::hash_table_factor = 1.3;
Frequent_seeds frequent_seeds;

void Frequent_seeds::compute_sd(atomic<unsigned> *seedp, DoubleArray<SeedArray::_pos> *query_seed_hits, DoubleArray<SeedArray::_pos> *ref_seed_hits, vector<Sd> *ref_out, vector<Sd> *query_out)
{
	unsigned p;
	while ((p = (*seedp)++) < current_range.end()) {
		Sd ref_sd, query_sd;
		for (auto it = JoinIterator<SeedArray::_pos>(query_seed_hits[p].begin(), ref_seed_hits[p].begin()); it; ++it) {
			query_sd.add((double)it.r->size());
			ref_sd.add((double)it.s->size());
		}
		(*ref_out)[p - current_range.begin()] = ref_sd;
		(*query_out)[p - current_range.begin()] = query_sd;
	}
}

void Frequent_seeds::build_worker(
	size_t seedp,
	size_t thread_id,
	DoubleArray<SeedArray::_pos> *query_seed_hits,
	DoubleArray<SeedArray::_pos> *ref_seed_hits,
	const SeedPartitionRange *range,
	unsigned sid,
	unsigned ref_max_n,
	unsigned query_max_n,
	vector<unsigned> *counts) {
	if (!range->contains((unsigned)seedp))
		return;

	vector<uint32_t> buf;
	size_t n = 0;
	for (auto it = JoinIterator<SeedArray::_pos>(query_seed_hits[seedp].begin(), ref_seed_hits[seedp].begin()); it;) {
		if (it.s->size() > ref_max_n || it.r->size() > query_max_n) {
			n += (unsigned)it.s->size();
			Packed_seed s;
			shapes[sid].set_seed(s, query_seqs::get().data(*it.r->begin()));
			buf.push_back(seed_partition_offset(s));

#ifdef SEQ_MASK
			if (config.fast_stage2) {
				Range<SeedArray::_pos*> query_hits = *it.r;
				for (SeedArray::_pos* i = query_hits.begin(); i < query_hits.end(); ++i) {
					Letter* p = query_seqs::get_nc().data(*i);
					*p |= SEED_MASK;
				}
			}
#endif

			it.erase();

		}
		else
			++it;
	}

	const size_t ht_size = std::max((size_t)(buf.size() * hash_table_factor), buf.size() + 1);
	PHash_set<void, murmur_hash> hash_set(ht_size);

	for (vector<uint32_t>::const_iterator i = buf.begin(); i != buf.end(); ++i)
		hash_set.insert(*i);

	frequent_seeds.tables_[sid][seedp] = std::move(hash_set);
	(*counts)[seedp] = (unsigned)n;
}

void Frequent_seeds::build(unsigned sid, const SeedPartitionRange &range, DoubleArray<SeedArray::_pos> *query_seed_hits, DoubleArray<SeedArray::_pos> *ref_seed_hits)
{
	vector<Sd> ref_sds(range.size()), query_sds(range.size());
	atomic<unsigned> seedp(range.begin());
	vector<std::thread> threads;
	for (unsigned i = 0; i < config.threads_; ++i)
		threads.emplace_back(compute_sd, &seedp, query_seed_hits, ref_seed_hits, &ref_sds, &query_sds);
	for (auto &t : threads)
		t.join();

	Sd ref_sd(ref_sds), query_sd(query_sds);
	const unsigned ref_max_n = (unsigned)(ref_sd.mean() + config.freq_sd*ref_sd.sd()), query_max_n = (unsigned)(query_sd.mean() + config.freq_sd*query_sd.sd());
	log_stream << "Seed frequency mean (reference) = " << ref_sd.mean() << ", SD = " << ref_sd.sd() << endl;
	log_stream << "Seed frequency mean (query) = " << query_sd.mean() << ", SD = " << query_sd.sd() << endl;
	log_stream << "Seed frequency cap query: " << query_max_n << ", reference: " << ref_max_n << endl;
	vector<unsigned> counts(Const::seedp);
	Util::Parallel::scheduled_thread_pool_auto(config.threads_, Const::seedp, build_worker, query_seed_hits, ref_seed_hits, &range, sid, ref_max_n, query_max_n, &counts);
	log_stream << "Masked positions = " << std::accumulate(counts.begin(), counts.end(), 0) << std::endl;
}