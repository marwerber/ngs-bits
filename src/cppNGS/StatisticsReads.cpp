#include "StatisticsReads.h"
#include "LinePlot.h"
#include "Helper.h"

StatisticsReads::StatisticsReads()
	: mutex_()
	, c_forward_(0)
	, c_reverse_(0)
	, read_lengths_()
	, c_read_q20_(0.0)
	, c_base_q30_(0.0)
	, pileups_()
	, qualities1_()
	, qualities2_()
{
}

void StatisticsReads::update(const FastqEntry& entry, ReadDirection direction)
{
	mutex_.lock();

	//update read counts
	if (direction==FORWARD)
	{
		++c_forward_;
	}
	else
	{
		++c_reverse_;
	}

	//check number of cycles
	int cycles = entry.bases.count();
	read_lengths_.insert(cycles);
	if (cycles>pileups_.size())
	{
		pileups_.resize(cycles);
		qualities1_.resize(cycles);
		qualities2_.resize(cycles);
	}

	//create pileups
	for (int i=0; i<cycles; ++i) pileups_[i].inc(entry.bases[i]);

	//handle qualities
	double q_sum = 0.0;
	for (int i=0; i<cycles; ++i)
	{
		int q = entry.quality(i);
		q_sum += q;
		if (q>=30.0) ++c_base_q30_;
		if (direction==FORWARD)
		{
			qualities1_[i] += q;
		}
		else
		{
			qualities2_[i] += q;
		}
	}
	if (q_sum/cycles>=20.0) ++c_read_q20_;

	mutex_.unlock();
}

QCCollection StatisticsReads::getResult()
{
	//create output values
	QCCollection output;

	int total_reads = c_forward_ + c_reverse_;
	double c_base_n = 0.0;
	double c_base_gc = 0.0;
	double bases_total = 0.0;
	foreach(const Pileup& pileup, pileups_)
	{
		c_base_n += pileup.n();
		c_base_gc += pileup.g() + pileup.c();
		bases_total += pileup.depth(false, true);
	}

	output.insert(QCValue("read count", total_reads, "Total number of reads (forward and reverse reads of paired-end sequencing count as two reads).", "QC:2000005"));
	QString lengths = "";
	QList<int> tmp = read_lengths_.toList(); //why? QSet is hash-based!
	std::sort(tmp.begin(), tmp.end()); 
	if (tmp.size()<4)
	{
		lengths = QString::number(tmp[0]);
		for (int i=1; i<tmp.size(); ++i)
		{
			lengths += ", " + QString::number(tmp[i]);
		}
	}
	else
	{
		lengths = QString::number(tmp[0]) + "-" + QString::number(tmp[tmp.size()-1]);
	}
	output.insert(QCValue("read length", lengths, "Raw read length of a single read before trimming. Comma-separated list of lenghs or length range, if reads have different lengths.", "QC:2000006"));
	output.insert(QCValue("Q20 read percentage", 100.0*c_read_q20_/total_reads, "The percentage of reads with a mean base quality score greater than Q20.", "QC:2000007"));
	output.insert(QCValue("Q30 base percentage", 100.0*c_base_q30_/bases_total, "The percentage of bases with a minimum quality score of Q30.", "QC:2000008"));
	output.insert(QCValue("no base call percentage", 100.0*c_base_n/bases_total, "The percentage of bases without base call (N).", "QC:2000009"));
	output.insert(QCValue("gc content percentage", 100.0*c_base_gc/(bases_total-c_base_n), "The percentage of bases that are called to be G or C.", "QC:2000010"));

	//create output base distribution plot
	int cycles = pileups_.count();
	QVector<double> line_a(cycles), line_c(cycles), line_g(cycles), line_t(cycles), line_n(cycles), line_gc(cycles), line_x(cycles);
	int i=0;
	foreach(const Pileup& pileup, pileups_)
	{
		double depth_no_n = pileup.depth(false);
		line_a[i] = 100.0 * pileup.a() / depth_no_n;
		line_c[i] = 100.0 * pileup.c() / depth_no_n;
		line_g[i] = 100.0 * pileup.g() / depth_no_n;
		line_t[i] = 100.0 * pileup.t() / depth_no_n;
		line_n[i] = 100.0 * pileup.n() / (depth_no_n + pileup.n());
		line_gc[i] = line_g[i] + line_c[i];
		line_x[i] = i+1;
		++i;
	}
	LinePlot plot;
	plot.setXLabel("cycle");
	plot.setYLabel("base [%]");
	plot.setYRange(0.0, 100.0);
	plot.setXValues(line_x);
	plot.addLine(line_a, "A");
	plot.addLine(line_c, "C");
	plot.addLine(line_g, "G");
	plot.addLine(line_t, "T");
	plot.addLine(line_n, "N");
	plot.addLine(line_gc, "GC");
	QString plotname = Helper::tempFileName(".png");
	plot.store(plotname);
	output.insert(QCValue::Image("base distribution plot", plotname, "Base distribution plot per cycle.", "QC:2000011"));
	QFile::remove(plotname);

	//create output quality distribution plot
	for(int j=0; j<qualities1_.count(); ++j)
	{
		int depth = pileups_[j].depth(false, true) / 2;
		qualities1_[j] /= depth;
		qualities2_[j] /= depth;
	}
	LinePlot plot2;
	plot2.setXLabel("cycle");
	plot2.setYLabel("mean Q score");
	plot2.setYRange(0.0, 41.5);
	plot2.setXValues(line_x);
	plot2.addLine(qualities1_, "forward reads");
	if (c_reverse_>0)
	{
		plot2.addLine(qualities2_, "reverse reads");
	}
	QString plotname2 = Helper::tempFileName(".png");
	plot2.store(plotname2);
	output.insert(QCValue::Image("Q score plot", plotname2, "Mean Q score per cycle for forward/reverse reads.", "QC:2000012"));
	QFile::remove(plotname);

	return output;
}
