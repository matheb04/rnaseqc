//
//  Metrics.cpp
//  IntervalTree
//
//  Created by Aaron Graubert on 7/5/17.
//  Copyright © 2017 Aaron Graubert. All rights reserved.
//

#include "Metrics.h"
#include <iostream>


void Metrics::increment(std::string key)
{
    this->counter[key]++;
}

void Metrics::increment(std::string key, int n)
{
    this->counter[key] += n;
}

unsigned long Metrics::get(std::string key)
{
    return this->counter[key];
}

double Metrics::frac(std::string a, std::string b)
{
    return static_cast<double>(this->get(a)) / this->get(b);
}

std::ofstream& operator<<(std::ofstream &stream, Metrics &counter)
{
    std::vector<std::string> keys =  {
        //"Alternative Alignments",
        //"Chimeric Reads",
        "Duplicate Reads",
        "End 1 Antisense",
        "End 2 Antisense",
        "End 1 Bases",
        "End 2 Bases",
        "End 1 Mapped Reads",
        "End 2 Mapped Reads",
        "End 1 Mismatches",
        "End 2 Mismatches",
        "End 1 Sense",
        "End 2 Sense",
        "Exonic Reads", //reads
        "Failed Vendor QC",
        // "Filtered by tag: ", //FIXME
        "Intergenic Reads",
        "Intragenic Reads",
        "Intron/Exon Disqualified Reads", //capitalize
        "Intronic Reads",
        "Low quality reads",
        "Mapped Duplicate Reads",
        "Mapped Reads",
        "Mapped Unique Reads",
        "Mismatched Bases", //bases
        "Reads excluded from exon counts",
        "Reads used for Intron/Exon counts",
        "rRNA Reads",
        "Split Reads",
        "Total Bases",
        "Total Mapped Pairs",
        "Total Reads",
        "Unique Mapping, Vendor QC Passed Reads",
        "Unpaired Reads"
    };
    stream << "Alternative Alignments\t" << counter.get("Alternative Alignments") << std::endl;
    stream << "Chimeric Reads\t";
    if (counter.get("Chimeric Reads_tag")) stream << counter.get("Chimeric Reads_tag") << std::endl;
    else stream << counter.get("Chimeric Reads_contig") << std::endl;
    for (int i = 0; i < keys.size(); ++i) stream << keys[i] << "\t" << counter.get(keys[i]) << std::endl;
    auto beg = counter.counter.begin();
    auto end = counter.counter.end();
    while (beg != end)
    {
        if( beg->first.length() > 17 && beg->first.substr(0,17) == "Filtered by tag: ")
        {
            stream << beg->first << "\t" << beg->second << std::endl;
        }
        ++beg;
    }
    return stream;
}

void Collector::add(const std::string &gene_id, const std::string &exon_id, const double coverage)
{
    if (coverage > 0)
    {
        this->data[gene_id].push_back(std::pair<std::string, double>(exon_id, coverage));
        this->dirty = true;
    }
}

void Collector::collect(const std::string &gene_id)
{
    for (auto entry = this->data[gene_id].begin(); entry != this->data[gene_id].end(); ++entry)
    {
        (*this->target)[entry->first] += entry->second;
        this->total += entry->second;
    }
}

void Collector::collectSingle(const std::string &gene_id)
{
    for (auto entry = this->data[gene_id].begin(); entry != this->data[gene_id].end(); ++entry)
    {
        (*this->target)[entry->first] += 1.0;
    }
}

bool Collector::queryGene(const std::string &gene_id)
{
    return static_cast<bool>(this->data[gene_id].size());
}

bool Collector::isDirty()
{
    return this->dirty;
}

double Collector::sum()
{
    return this->total;
}

void BaseCoverage::add(const Feature &exon, const coord start, const coord end) //Adds to the cache
{
    CoverageEntry tmp;
    tmp.offset = start - exon.start;
    tmp.length = end - start;
    tmp.transcript_id = exon.transcript_id;
    tmp.feature_id = exon.feature_id;
    this->cache[exon.gene_id].push_back(tmp);
}

void BaseCoverage::commit(const std::string &gene_id) //moves one gene out of the cache to permanent storage
{
    auto beg = this->cache[gene_id].begin();
    auto end = this->cache[gene_id].end();
    while (beg != end)
    {
        CoverageEntry tmp;
        tmp.offset = beg->offset;
        tmp.length = beg->length;
        tmp.transcript_id = beg->transcript_id;
        tmp.feature_id = gene_id;
        this->coverage[beg->feature_id].push_back(tmp);
        ++beg;
    }
}

void BaseCoverage::reset() //Empties the cache
{
    this->cache.clear();
}

void BaseCoverage::dump(const Feature &exon) //Dumps one exon to the tmp file
{
    auto beg = this->coverage[exon.feature_id].begin();
    auto end = this->coverage[exon.feature_id].end();
    while (beg != end)
    {
        this->writer << /*beg->feature_id << "\t" <<*/ beg->transcript_id << "\t" << exon.feature_id << "\t";
        this->writer << beg->offset << "\t" << beg->length << std::endl;
        ++beg;
    }
    this->coverage.erase(this->coverage.find(exon.feature_id));
}

void BaseCoverage::close()
{
    this->writer.flush();
    this->writer.close();
}

void BiasCounter::checkBias(Feature &gene, Feature &block)
{
    if (geneLengths[gene.feature_id] < this->geneLength) return;
    Feature tmp;
    //adjust the start and end coordinates with the offset
    tmp.start = gene.start + this->offset;
    tmp.end = gene.end - this->offset;
    //bool if the block intersects the left window of the gene.
    bool leftEnd = block.start <= tmp.start + this->windowSize && block.start >= tmp.start;
    if (leftEnd || (block.end >= tmp.end - this->windowSize && block.end <= tmp.end))
    {
        //Key:
        // + strand, left end -> 5'
        // + strand, right end -> 3'
        // - strand, left end -> 3'
        // - strand, right end -> 5'
//        Feature target;
//        target.start = leftEnd ? gene.start : gene.end - this->windowSize;
//        target.end = leftEnd ? gene.start + this->windowSize : gene.end;
        if ((gene.strand == 1)^leftEnd) this->threeEnd[gene.feature_id] += partialIntersect(tmp, block);
        else this->fiveEnd[gene.feature_id] += partialIntersect(tmp, block);
    }
}

double BiasCounter::getBias(const std::string &geneID)
{
    double cov5 = this->fiveEnd[geneID];
    double cov3 = this->threeEnd[geneID];
    if (cov5 + cov3 > 0.0) return cov3 / (cov5 + cov3);
    return -1.0;
}
