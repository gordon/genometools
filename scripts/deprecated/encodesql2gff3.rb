#!/usr/bin/env ruby

$:.push(File.dirname($0))
require 'gff3'

# read input
sequences = {}
ARGF.each do |line|
  name, chrom, strand, txStart, txEnd, cdsStart, cdsEnd, exonCount, exonStarts, 
  exonEnds, id, name2, cdsStartStat, cdsEndStat, exonFrames  = line.split
  if sequences[chrom] then
     sequences[chrom].update_range(txStart.to_i+1, txEnd.to_i)
  else
    sequences[chrom] = Sequence.new(txStart.to_i+1, txEnd.to_i)
  end
  gene = Gene.new(Range.new(txStart.to_i+1, txEnd.to_i), strand)
  gene.name = name
  gene.attributes = { "Name2" => name2 }
  if (cdsStart.to_i > 0 and cdsEnd.to_i > 0) then
    gene.cds_pos = Range.new(cdsStart.to_i+1, cdsEnd.to_i)
  end
  exon_start_pos = exonStarts.split(',')
  exon_end_pos = exonEnds.split(',')
  1.upto(exonCount.to_i) do |i|
    gene.add_exon(Range.new(exon_start_pos[i-1].to_i+1, exon_end_pos[i-1].to_i));
  end
  sequences[chrom].add_gene(gene);
end

# output
gff3_output(sequences, "ENCODE")
