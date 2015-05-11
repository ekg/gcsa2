/*
  Copyright (c) 2015 Genome Research Ltd.

  Author: Jouni Siren <jouni.siren@iki.fi>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#ifndef _GCSA_GCSA_H
#define _GCSA_GCSA_H

#include "support.h"

namespace gcsa
{

//------------------------------------------------------------------------------

class GCSA
{
public:
  typedef gcsa::size_type  size_type;
  typedef sdsl::wt_huff<>  bwt_type;
  typedef sdsl::bit_vector bit_vector;

//------------------------------------------------------------------------------

  GCSA();
  GCSA(const GCSA& g);
  GCSA(GCSA&& g);
  ~GCSA();

  void swap(GCSA& g);
  GCSA& operator=(const GCSA& g);
  GCSA& operator=(GCSA&& g);

  size_type serialize(std::ostream& out, sdsl::structure_tree_node* v = nullptr, std::string name = "") const;
  void load(std::istream& in);

  const static std::string EXTENSION;  // .gcsa

  const static size_type DOUBLING_STEPS = 3;

  const static char_type ENDMARKER = '$';
  const static char_type ENDMARKER_COMP = 0;
  const static char_type SOURCE_MARKER = '#';

//------------------------------------------------------------------------------

  /*
    The input vector must be sorted and contain only unique kmers of length 16 or less.
    Each kmer must have at least one predecessor and one successor.
  */
  GCSA(const std::vector<key_type>& keys, size_type kmer_length, const Alphabet& _alpha = Alphabet());

  /*
    This is the main constructor. We build GCSA from the kmers, doubling the path length
    three times. The kmer array will be cleared during the construction.

    FIXME option to change the number of doubling steps
  */
  GCSA(std::vector<KMer>& kmers, size_type kmer_length, const Alphabet& _alpha = Alphabet());

//------------------------------------------------------------------------------

  /*
    The high-level interface deals with node ranges and actual character values.
    locate() stores the node identifiers in the given vector in sorted order.
    If append is true, the results are appended to the existing vector.
    If sort is true, the results are sorted and the duplicates are removed.

    The implementation of find() is based on random access iterators. Bidirectional
    iterators would be enough without the query length check.
  */

  template<class Iterator>
  range_type find(Iterator begin, Iterator end) const
  {
    if(begin == end) { return range_type(0, this->size() - 1); }
    if((size_type)(end - begin) > this->order())
    {
      std::cerr << "GCSA::find(): Query length exceeds " << this->order() << std::endl;
      return range_type(1, 0);
    }

    --end;
    range_type range = this->nodeRange(gcsa::charRange(this->alpha, this->alpha.char2comp[*end]));
    while(!Range::empty(range) && end != begin)
    {
      --end;
      range = this->LF(range, this->alpha.char2comp[*end]);
    }

    return range;
  }

  template<class Container>
  range_type find(const Container& pattern) const
  {
    return this->find(pattern.begin(), pattern.end());
  }

  range_type find(const char_type* pattern, size_type length) const;

  void locate(size_type node, std::vector<node_type>& results, bool append = false, bool sort = true) const;
  void locate(range_type range, std::vector<node_type>& results, bool append = false, bool sort = true) const;

//------------------------------------------------------------------------------

  /*
    The low-level interface deals with nodes, node ranges, and contiguous character values.
  */

  inline size_type size() const { return this->node_count; }
  inline size_type edge_count() const { return this->bwt.size(); }
  inline size_type order() const { return this->max_query_length; }

  inline bool has_samples() const { return (this->stored_samples.size() > 0); }

  inline range_type charRange(char_type comp) const
  {
    return this->nodeRange(gcsa::charRange(this->alpha, comp));
  }

  inline size_type LF(size_type node, char_type comp) const
  {
    node = this->startPos(node);
    node = gcsa::LF(this->bwt, this->alpha, node, comp);
    return this->edge_rank(node);
  }

  inline range_type LF(range_type range, char_type comp) const
  {
    range = this->bwtRange(range);
    range = gcsa::LF(this->bwt, this->alpha, range, comp);
    if(Range::empty(range)) { return range; }
    return this->nodeRange(range);
  }

  // Follow the first edge backwards.
  inline size_type LF(size_type node) const
  {
    node = this->startPos(node);
    auto temp = this->bwt.inverse_select(node);
    node = this->alpha.C[temp.second] + temp.first;
    return this->edge_rank(node);
  }

//------------------------------------------------------------------------------

  /*
    We could have a bitvector for mapping between the node identifiers returned by locate()
    and the original (node, offset) pairs. Alternatively, if the maximum length of a label
    is reasonable, we can just use (node, offset) = (id / max_length, id % max_length).
  */

  size_type                 node_count;
  size_type                 max_query_length;

  bwt_type                  bwt;
  Alphabet                  alpha;

  // The last BWT position in each node is marked with an 1-bit.
  bit_vector                nodes;
  bit_vector::rank_1_type   node_rank;
  bit_vector::select_1_type node_select;

  // The last outgoing edge from each node is marked with an 1-bit.
  bit_vector                edges;
  bit_vector::rank_1_type   edge_rank;
  bit_vector::select_1_type edge_select;

  // Nodes containing samples are marked with an 1-bit.
  bit_vector                sampled_nodes;
  bit_vector::rank_1_type   sampled_node_rank;

  // The last sample belonging to the same node is marked with an 1-bit.
  sdsl::int_vector<0>       stored_samples;
  bit_vector                samples;
  bit_vector::select_1_type sample_select;

//------------------------------------------------------------------------------

private:
  void copy(const GCSA& g);
  void setVectors();

  /*
    Increase path length to 2^DOUBLING_STEPS times the original and set max_query_length.
    Return the actual path length multiplier.
  */
  size_type prefixDoubling(std::vector<PathNode>& paths, size_type kmer_length);

  /*
    Merge paths having the same label and build the samples. Sets node_count and all
    structures related to samples.

    We sample a path node, if it a) has multiple predecessors; b) is the source node;
    or c) has offset 0 for at least one from node.

    FIXME Later: An alternate sampling scheme for graphs where nodes are not (id, offset)
    pairs. We sample a path node, if it a) has multiple predecessors; b) is the source
    node; or c) its node value is anything other than the value of the predecessor + 1
    (for one or more nodes). We may also add samples, if the distance to the next sample
    is too large.
  */
  void sample(std::vector<PathNode>& paths, size_type path_order);

  /*
    Store the number of outgoing edges in the to fields of each node.
  */
  size_type countEdges(std::vector<PathNode>& paths, size_type path_order, size_type sigma,
    const GCSA& mapper, const sdsl::int_vector<0>& last_char);

  void locateInternal(size_type node, std::vector<node_type>& results) const;

//------------------------------------------------------------------------------

  struct KeyGetter
  {
    inline static size_type predecessors(key_type key) { return Key::predecessors(key); }
    inline static size_type outdegree(key_type key)
    {
      return sdsl::bits::lt_cnt[Key::successors(key)];
    }
  };

  struct PathNodeGetter
  {
    inline static size_type predecessors(const PathNode& path) { return path.predecessors(); }
    inline static size_type outdegree(const PathNode& path) { return path.outdegree(); }
  };

  template<class NodeType, class Getter>
  void build(const std::vector<NodeType>& nodes, const Alphabet& _alpha, size_type total_edges)
  {
    sdsl::int_vector<64> counts(_alpha.sigma, 0);
    sdsl::int_vector<8> buffer(total_edges, 0);
    this->nodes = bit_vector(total_edges, 0);
    this->edges = bit_vector(total_edges, 0);
    for(size_type i = 0, bwt_pos = 0, edge_pos = 0; i < nodes.size(); i++)
    {
      byte_type pred = Getter::predecessors(nodes[i]);
      for(size_type j = 0; j < _alpha.sigma; j++)
      {
        if(pred & (((size_type)1) << j))
        {
          buffer[bwt_pos] = j; bwt_pos++;
          counts[j]++;
        }
      }
      this->nodes[bwt_pos - 1] = 1;
      edge_pos += Getter::outdegree(nodes[i]);
      this->edges[edge_pos - 1] = 1;
    }
    directConstruct(this->bwt, buffer);
    this->alpha = Alphabet(counts, _alpha.char2comp, _alpha.comp2char);

    sdsl::util::init_support(this->node_rank, &(this->nodes));
    sdsl::util::init_support(this->node_select, &(this->nodes));
    sdsl::util::init_support(this->edge_rank, &(this->edges));
    sdsl::util::init_support(this->edge_select, &(this->edges));
  }

//------------------------------------------------------------------------------

  inline size_type startPos(size_type node) const
  {
    return (node > 0 ? this->node_select(node) + 1 : 0);
  }

  inline size_type endPos(size_type node) const
  {
    return this->node_select(node + 1);
  }

  inline range_type bwtRange(range_type node_range) const
  {
    node_range.first = this->startPos(node_range.first);
    node_range.second = this->endPos(node_range.second);
    return node_range;
  }

  inline range_type nodeRange(range_type incoming_range) const
  {
    incoming_range.first = this->edge_rank(incoming_range.first);
    incoming_range.second = this->edge_rank(incoming_range.second);
    return incoming_range;
  }

  inline range_type sampleRange(size_type node) const
  {
    node = this->sampled_node_rank(node);
    range_type sample_range;
    sample_range.first = (node > 0 ? this->sample_select(node) + 1 : 0);
    sample_range.second = this->sample_select(node + 1);
    return sample_range;
  }
};  // class GCSA

//------------------------------------------------------------------------------

} // namespace gcsa

#endif // _GCSA_GCSA_H
