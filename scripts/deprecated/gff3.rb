class Gene
  def initialize(range, strand)
    @range = range
    if not (strand == '+' or strand == '-') then
      raise "'#{strand}' is not a valid strand"
    end
    @strand = strand
    @exons = []
  end
  def add_exon(range)
    @exons.push(range)
  end
  attr_reader :range, :strand, :exons
  attr_accessor :name, :attributes, :cds_pos
end

class Sequence
  def initialize(start_pos, end_pos)
    @start_pos = start_pos
    @end_pos = end_pos
    @genes = []
  end
  def update_range(start_pos, end_pos)
    @start_pos = start_pos if start_pos < @start_pos
    @end_pos = end_pos if end_pos > @end_pos
  end
  def add_gene(gene) 
    @genes.push(gene)
  end
  attr_accessor :start_pos, :end_pos
  attr_reader :genes
end

def gff3_output(sequences, source)
  gene_number = 1
  puts "##gff-version   3"
  sequences.each do |name, seq|
    puts "##sequence-region #{name} #{seq.start_pos} #{seq.end_pos}"
    seq.genes.each do |gene|
      print "#{name}	#{source}	gene	#{gene.range.begin}	"+
            "#{gene.range.end}	.	#{gene.strand}	.	"+
            "ID=gene#{gene_number}"
      if gene.name then
        print ";Name=#{gene.name}"
      end
      if gene.attributes then 
        gene.attributes.each do |attr_name, attr_value|
          print ";#{attr_name}=#{attr_value}"
        end
      end
      print "\n"
      exon_number = 1
      gene.exons.each do |exon|
        puts "#{name}	#{source}	exon	#{exon.begin}	#{exon.end}	"+
             ".	#{gene.strand}	.	Parent=gene#{gene_number}"
        if (gene.cds_pos) then
          max_start = gene.cds_pos.begin > exon.begin ? gene.cds_pos.begin \
                                                      : exon.begin 
          min_end = gene.cds_pos.end < exon.end ? gene.cds_pos.end : exon.end
          if (gene.cds_pos.begin <= exon.end and gene.cds_pos.end >= exon.begin)
          then
            puts "#{name}	#{source}	CDS	" +
                 "#{max_start}	#{min_end}	.	#{gene.strand}	.	Parent=gene#{gene_number}"
          end
        end
      end
      gene_number += 1
    end
    puts "###"
  end
end
