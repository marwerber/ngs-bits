#include "ToolBase.h"
#include "Auxilary.h"
#include "AnalysisWorker.h"
#include "Helper.h"
#include <QThreadPool>
#include <QTime>

class ConcreteTool
		: public ToolBase
{
	Q_OBJECT

public:
	ConcreteTool(int& argc, char *argv[])
		: ToolBase(argc, argv)
		, params_()
		, stats_()
		, data_()
	{
	}

	virtual void setup()
	{
		setDescription("Removes adapter sequences from paired-end sequencing data.");
		addInfileList("in1", "Forward input gzipped FASTQ file(s).", false);
		addInfileList("in2", "Reverse input gzipped FASTQ file(s).", false);
		addOutfile("out1", "Forward output gzipped FASTQ file.", false);
		addOutfile("out2", "Reverse output gzipped FASTQ file.", false);
		//optional
		addString("a1", "Forward adapter sequence (at least 15 bases).", true, "AGATCGGAAGAGCACACGTCTGAACTCCAGTCACGAGTTA");
		addString("a2", "Reverse adapter sequence (at least 15 bases).", true, "AGATCGGAAGAGCGTCGTGTAGGGAAAGAGTGTAGATCTC");
		addFloat("match_perc", "Minimum percentage of matching bases for sequence/adapter matches.", true, 80.0);
		addFloat("mep", "Maximum error probability of insert and adapter matches.", true, 0.000001);
		addInt("qcut", "Quality trimming cutoff for trimming from the end of reads using a sliding window approach. Set to 0 to disable.", true, 15);
		addInt("qwin", "Quality trimming window size.", true, 5);
		addInt("qoff", "Quality trimming FASTQ score offset.", true, 33);
		addInt("ncut", "Number of subsequent Ns to trimmed using a sliding window approach from the front of reads. Set to 0 to disable.", true, 7);
		addInt("min_len", "Minimum read length after adapter trimming. Shorter reads are discarded.", true, 15);
		addInt("threads", "The number of threads used for trimming (an additional thread is used for reading data).", true, 1);
		addOutfile("out3", "Name prefix of singleton read output files (if only one read of a pair is discarded).", true, false);
		addOutfile("summary", "Write summary/progress to this file instead of STDOUT.", true, true);
		addOutfile("qc", "If set, a read QC file in qcML format is created (just like ReadQC).", true, true);
		addInt("prefetch", "Maximum number of reads that may be pre-fetched to speed up trimming", true, 1000);
		addFlag("ec", "Enable error-correction of adapter-trimmed reads (only those with insert match).");
		addFlag("debug", "Enables debug output (use only with one thread).");
		addFlag("progress", "Enables progress output.");

		//changelog
		changeLog(2016, 4, 15, "Removed large part of the overtrimming described in the paper (~75% of reads overtrimmed, ~50% of bases overtrimmed).");
		changeLog(2016, 4,  6, "Added error correction (optional).");
		changeLog(2016, 3, 16, "Version used in the SeqPurge paper: http://bmcbioinformatics.biomedcentral.com/articles/10.1186/s12859-016-1069-7");
	}

	int processingReadPairs() const
	{
		return data_.reads_queued - stats_.read_num;
	}

	virtual void main()
	{
		//init
		QStringList in1_files = getInfileList("in1");
		QStringList in2_files = getInfileList("in2");
		if (in1_files.count()!=in2_files.count())
		{
			THROW(CommandLineParsingException, "Input file lists 'in1' and 'in2' differ in counts!");
		}

		data_.out1 = new FastqOutfileStream(getOutfile("out1"), false);
		data_.out2 = new FastqOutfileStream(getOutfile("out2"), false);

		params_.a1 = getString("a1").trimmed().toLatin1();
		if (params_.a1.count()<15) THROW(CommandLineParsingException, "Forward adapter " + params_.a1 + " too short!");
		params_.a2 = getString("a2").trimmed().toLatin1();
		if (params_.a2.count()<15) THROW(CommandLineParsingException, "Reverse adapter " + params_.a2 + " too short!");
		params_.a_size = std::min(20, std::min(params_.a1.count(), params_.a2.count()));

		params_.match_perc = getFloat("match_perc");
		params_.mep = getFloat("mep");
		params_.min_len = getInt("min_len");
		params_.max_reads_queued = getInt("prefetch");

		params_.qcut = getInt("qcut");
		params_.qwin = getInt("qwin");
		params_.qoff = getInt("qoff");
		params_.ncut = getInt("ncut");

		QString out3_base = getOutfile("out3").trimmed();
		if (out3_base!="")
		{
			data_.out3 = new FastqOutfileStream(out3_base + "_R1.fastq.gz", false);
			data_.out4 = new FastqOutfileStream(out3_base + "_R2.fastq.gz", false);
		}

		params_.qc = getOutfile("qc");
		data_.analysis_pool.setMaxThreadCount(getInt("threads"));
		params_.ec = getFlag("ec");
		params_.debug = getFlag("debug");

		QSharedPointer<QFile> outfile = Helper::openFileForWriting(getOutfile("summary"), true);
		QTextStream out(outfile.data());
		bool progress = getFlag("progress");
		QTime timer;
		if (progress) timer.start();

		//init pre-calculation of faktorials
		AnalysisWorker::precalculateFactorials();

		//process
		for (int i=0; i<in1_files.count(); ++i)
		{
			if (progress) out << Helper::dateTime() << " starting - forward: " << in1_files[i] << " reverse: " << in2_files[i] << endl;

			FastqFileStream in1(in1_files[i], false);
			FastqFileStream in2(in2_files[i], false);
			while (!in1.atEnd() && !in2.atEnd())
			{
				//prevent caching of too many reads (waste of memory)
				while(processingReadPairs()>=params_.max_reads_queued)
				{
					QThread::msleep(1);

					if (progress && timer.elapsed()>10000)
					{
						out << Helper::dateTime() << " waiting - processing now: " << processingReadPairs() << " total processed: " << data_.reads_queued << endl;
						timer.restart();
					}
				}

				if (progress && timer.elapsed()>10000)
				{
					out << Helper::dateTime() << " reading - processing now: " << processingReadPairs() << " total processed: " << data_.reads_queued << endl;
					timer.restart();
				}
				data_.reads_queued += 2;

				QSharedPointer<FastqEntry> e1(new FastqEntry());
				in1.readEntry(*e1);
				QSharedPointer<FastqEntry> e2(new FastqEntry());
				in2.readEntry(*e2);

				data_.analysis_pool.start(new AnalysisWorker(e1, e2, params_, stats_, ecstats_, data_));
			}

			//check that forward and reverse read file are both at the end
			if (!in1.atEnd())
			{
				THROW(FileParseException, "File " + in1_files[i] + " has more entries than " + in2_files[i] + "!");
			}
			if (!in2.atEnd())
			{
				THROW(FileParseException, "File " + in2_files[i] + " has more entries than " + in1_files[i] + "!");
			}
		}

		//close streams
		if (progress) out << Helper::dateTime() << " closing - processing now: " << processingReadPairs() << " total processed: " << data_.reads_queued << endl;
		data_.closeOutStreams();
		if (progress) out << Helper::dateTime() << " closed - processing now: " << processingReadPairs() << " total processed: " << data_.reads_queued << endl;

		//print trimming statistics
		if (progress) out << Helper::dateTime() << " writing statistics summary" << endl;
		stats_.writeStatistics(out, params_);

		//write qc output file
		if (!params_.qc.isEmpty())
		{
			stats_.qc.getResult().storeToQCML(getOutfile("qc"), QStringList() << in1_files << in2_files, "");
		}

		//print error correction statistics
		if (params_.ec)
		{
			if (progress) out << Helper::dateTime() << " writing error corrections summary" << endl;
			ecstats_.writeStatistics(out);
		}
	}

private:
	TrimmingParameters params_;
	TrimmingStatistics stats_;
	ErrorCorrectionStatistics ecstats_;
	TrimmingData data_;
};

#include "main.moc"

int main(int argc, char *argv[])
{
	ConcreteTool tool(argc, argv);
	return tool.execute();
}
