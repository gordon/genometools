#!/usr/bin/env ruby

$:.push(File.dirname($0))
require 'gff3'

if ARGV.size == 0
  raise "Usage: #{$0} contig_file [file ...]"
end

#contig_file = ARGV[0]
#ARGV.delete_at(0)

class Scanner
  def initialize(input)
    @input = input
    @line_buf = nil
  end
  def peek
    if @line_buf then
      return @line_buf
    end 
    @line_buf = @input.gets
  end
  def gets
    if @line_buf then
      buf = @line_buf
      @line_buf = nil
      return buf
    end
    @input.gets
  end
  def lineno
    @input.lineno
  end
  def filename
    @input.filename
  end
end

class GMAPParser
  def initialize(sequences, coordinate_mapping = nil)
    @sequences = sequences
    @coordinate_mapping = coordinate_mapping
    @current_accession = nil
    @current_strand = nil
  end
  def parse(scanner)
    while scanner.peek
      if scanner.gets =~ /^>/ then
        if parse_paths(scanner) then
          parse_alignment(scanner)
        end
      end
    end
  end
  private
  def parse_paths(scanner)
    line = scanner.gets
    if line =~ /^Paths/ then
      # XXX: change parser to handle multiple paths
      if line =~ /^Paths \(1\):/ then
        while line = scanner.gets do   
          if line =~ /Genomic pos:/ then
            if line =~ /^.*\(([+-])/ then
              @current_strand = $1
            else
              raise "could not parse strand on line #{scanner.lineno} of file \
                     #{scanner.filename}"
            end
          elsif line =~ /Accessions:/ then
            accession_parts = line.split(':')
            @current_accession = accession_parts[1].strip
            return true
          end
        end
      elsif line !~ /^Paths \(0\):/
        STDERR.puts "warning: skipping multiple path alignment on line \
                     #{scanner.lineno} of file #{scanner.filename}"
        return false
      end
    else
      raise "expecting 'Paths' on line #{scanner.lineno} of file \
             #{scanner.filename}"
    end 
  end
  def parse_alignment(scanner)
    while line = scanner.gets do
      if line =~ /^Alignments:/ then
        # skip two lines
        scanner.gets
        scanner.gets
        exons = []
        while (line = scanner.gets) =~ /:/ do
          # process all alignment lines
          line =~ /^.*:(\d+)-(\d+)/
          exon = Range.new($1.to_i, $2.to_i)
          if exon.begin > exon.end then
            exon = Range.new(exon.end, exon.begin)
          end
          if @coordinate_mapping and
             @coordinate_mapping[@current_accession] then
            exon = Range.new(exon.begin - \
                             @coordinate_mapping[@current_accession] + 1,
                             exon.end - \
                             @coordinate_mapping[@current_accession] + 1)
          end
          exons.push(exon)
        end
        if exons.size == 0 then
          raise "could not parse exon up to line #{scanner.lineno} in file \
                 #{scanner.filename}"
        end
        # construct gene
        exon_border = []
        exon_border.push(exons.first.begin)
        exon_border.push(exons.last.end)
        gene_range = Range.new(exon_border.min, exon_border.max)
        raise if gene_range.begin > gene_range.end
        if current_sequence = @sequences[@current_accession] then
          # the current sequence exists already -> make sure it is maximal
          current_sequence.update_range(gene_range.begin, gene_range.end)
        else
          # the current sequence does not exist -> add it
          @sequences[@current_accession] = Sequence.new(gene_range.begin, \
                                                        gene_range.end)
        end
        current_sequence =  @sequences[@current_accession]
        gene = Gene.new(gene_range, @current_strand)
        exons.each { |exon| gene.add_exon(exon) }
        current_sequence.add_gene(gene)
        return
      end
    end
  end
end

def compute_coordinate_mapping(filename)
  mapping = {}
  File.open(filename).each do |line|
    #puts line
    if line =~ /^(\S+)\s+(\d+)/ then
      #puts $1, $2
      mapping[$1] = $2.to_i
    end
  end
  mapping
end

# read input
sequences = {}
#gmap_parser = GMAPParser.new(sequences, compute_coordinate_mapping(contig_file))
gmap_parser = GMAPParser.new(sequences)
gmap_parser.parse(Scanner.new(ARGF))

# output
gff3_output(sequences, "gmap")
