#!/usr/bin/env ruby

require 'getoptlong'

$:.push(File.dirname($0))
require 'gff3'

# parse options
opts = GetoptLong.new(
  [ "--max-mismatches", "-m", GetoptLong::REQUIRED_ARGUMENT ]
)

max_mismatches = nil
opts.each do |opt, arg|
  raise if opt != "--max-mismatches"
  max_mismatches = arg.to_i
end

# read input
sequences = {}
ARGF.each do |line|
  matches, misMatches, repMatches, nCount, qNumInsert, qBaseInsert, \
  tNumInsert, tBaseInsert, strand, qName, qSize, qStart, qEnd, tName, tSize, \
  tStart, tEnd, blockCount, blockSizes, qStarts, tStarts = line.split
  if not max_mismatches or misMatches.to_i <= max_mismatches then
    if sequences[tName] then
      sequences[tName].update_range(tStart.to_i, tEnd.to_i)
    else
      sequences[tName] = Sequence.new(tStart.to_i, tEnd.to_i)
    end
    #STDERR.puts misMatches
    gene = Gene.new(Range.new(tStart.to_i + 1, tEnd.to_i + 1), strand[0..0].to_s)
    exon_start_pos = tStarts.split(',')
    exon_sizes = blockSizes.split(',')
    1.upto(blockCount.to_i) do |i|
      gene.add_exon(Range.new(exon_start_pos[i-1].to_i + 1, \
                              exon_start_pos[i-1].to_i + exon_sizes[i-1].to_i));
    end
    sequences[tName].add_gene(gene);
  end
end

# output
gff3_output(sequences, "blat")
