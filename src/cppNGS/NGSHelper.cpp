#include "NGSHelper.h"
#include "Exceptions.h"
#include "ChromosomeInfo.h"
#include <QTextStream>
#include <QFileInfo>
#include <QDateTime>
#include "math.h"

void NGSHelper::openBAM(BamReader& reader, QString bam_file)
{
	//open BAM file
	if (!reader.Open(bam_file.toStdString()))
	{
		THROW(FileAccessException, "Could not open BAM file " + bam_file + "!");
	}

	//open BAI file
	QString bai_file = bam_file + ".bai";
	if (!QFile::exists(bai_file))
	{
		THROW(FileAccessException, "BAI file missing " + bai_file + ".");
	}

	//abort BAI file is outdated (1 min margin because the order of SVN checkouts is not defined - BAM can be checked out after BAI)
	if (QFileInfo(bam_file).lastModified() > QFileInfo(bai_file).lastModified().addSecs(60))
	{
		THROW(FileAccessException, "BAI file outdated " + bai_file + ".");
	}

	//try to load the index
	if (!reader.OpenIndex(bai_file.toStdString()))
	{
		THROW(FileAccessException, "Could not open BAI file " + QString::fromStdString(reader.GetErrorString()));
	}
}

Pileup NGSHelper::getPileup(BamTools::BamReader& reader, const Chromosome& chr, int pos, int indel_window, int min_mapq, bool anom, int min_baseq)
{
	//init
	Pileup output;
	int reads_mapped = 0;
	int reads_mapq0 = 0;

	//restrict region
	int ref_id = ChromosomeInfo::refID(reader, chr);
	bool jump_ok = reader.SetRegion(ref_id, pos-1, ref_id, pos);
	if (!jump_ok) THROW(FileAccessException, QString::fromStdString(reader.GetErrorString()));

	//iterate through all alignments and create counts
	BamAlignment al;
	while (reader.GetNextAlignmentCore(al))
	{
		if (!al.IsProperPair() && anom==false) continue;
		if (!al.IsPrimaryAlignment()) continue;
		if (al.IsDuplicate()) continue;
		if (!al.IsMapped()) continue;

		reads_mapped += 1;
		if (al.MapQuality==0) reads_mapq0 += 1;

		if (al.MapQuality<min_mapq) continue;
		al.BuildCharData();

		//snps
		QPair<char, int> base = NGSHelper::extractBaseByCIGAR(al, pos);
		if (base.second>=min_baseq)
		{
			output.inc(base.first);
		}

		//indels
		if (indel_window>=0)
		{
			QVector<Sequence> indels;
			NGSHelper::extractIndelsByCIGAR(indels, al, pos, indel_window);
			output.addIndels(indels);
		}
	}

	output.setMapq0Frac((double)reads_mapq0 / reads_mapped);

	return output;
}

void NGSHelper::getPileups(QList<Pileup>& pileups, BamReader& reader, const Chromosome& chr, int start, int end, int min_mapq)
{
	//init empty pileups
	pileups.clear();
	pileups.reserve(end-start+1);
	for (int i=start; i<=end; ++i)
	{
		pileups.append(Pileup());
	}

	//restrict region
	int ref_id = ChromosomeInfo::refID(reader, chr);
	bool jump_ok = reader.SetRegion(ref_id, start-1, ref_id, end);
	if (!jump_ok) THROW(FileAccessException, QString::fromStdString(reader.GetErrorString()));

	//iterate through all alignments and create counts
	BamAlignment al;
	while (reader.GetNextAlignmentCore(al))
	{
		if (al.IsDuplicate()) continue;
		if (!al.IsProperPair()) continue;
		if (!al.IsPrimaryAlignment()) continue;
		if (!al.IsMapped() || al.MapQuality<min_mapq) continue;
		al.BuildCharData();

		//look up indels
		int read_pos = 0;
		int genome_pos = al.Position+1; //convert to 1-based position
		for (unsigned int i=0; i<al.CigarData.size(); ++i)
		{
			const CigarOp& op = al.CigarData[i];

			//update positions
			if (op.Type=='M')
			{
				for (unsigned int j=0; j<op.Length; ++j)
				{
					if (genome_pos>=start)
					{
						if (genome_pos>end) break;
						pileups[genome_pos-start].inc(al.QueryBases[read_pos]);
					}

					genome_pos += 1;
					read_pos += 1;
				}
			}
			else if(op.Type=='I')
			{
				if (genome_pos>=start)
				{
					pileups[genome_pos-start].addIndel(QByteArray("+") + al.QueryBases.substr(read_pos, op.Length).c_str());
				}

				read_pos += op.Length;
			}
			else if(op.Type=='D')
			{
				if (genome_pos>=start)
				{
					pileups[genome_pos-start].addIndel("-" + QByteArray::number(op.Length));
				}
				for (unsigned int j=0; j<op.Length; ++j)
				{
					if (genome_pos>=start)
					{
						if (genome_pos>end) break;
						pileups[genome_pos-start].inc('-');
					}

					genome_pos += 1;
				}
			}
			else if(op.Type=='N') //skipped reference bases (for RNA)
			{
				genome_pos += op.Length;
			}
			else if(op.Type=='S') //soft-clipped (only at the beginning/end)
			{
				read_pos += op.Length;
			}
			else if(op.Type=='H') //hard-clipped (only at the beginning/end)
			{
				//can be irgnored as hard-clipped bases are not considered in the position or sequence
			}
			else
			{
				THROW(Exception, "Unknown CIGAR operation " + QString(QChar(op.Type)) + "!");
			}

			//abort if we are right of the ROI
			if (genome_pos>end) break;
		}
	}
}

VariantDetails NGSHelper::getVariantDetails(BamReader& reader, const FastaFileIndex& reference, const Variant& variant)
{
	VariantDetails output;

	if (variant.isSNV()) //SVN
	{
		Pileup pileup = NGSHelper::getPileup(reader, variant.chr(), variant.start(), -1);
		output.depth = pileup.depth(true);
		if (output.depth!=0)
		{
			output.frequency = pileup.countOf(variant.obs()[0]) / (double)output.depth;
		}
		output.mapq0_frac = pileup.mapq0Frac();
	}
	else //indel
	{
		//determine region of interest for indel (important for repeat regions where more than one alignment is possible)
		QPair<int, int> reg = Variant::indelRegion(variant.chr(), variant.start(), variant.end(), variant.ref(), variant.obs(), reference);
		//qDebug() << "VAR:" << variant;
		//qDebug() << "REG:" << reg.first << reg.second;

		//get indels from region
		QVector<Sequence> indels;
		NGSHelper::getIndels(reference, reader, variant.chr(), reg.first-1, reg.second+1, indels, output.depth, output.mapq0_frac);
		//qDebug() << "INDELS:" << indels.join(" ");

		//count indels
		if (variant.ref()!="-" && variant.obs()!="-")
		{
			int c_ins = 0;
			int c_del = 0;
			foreach(const Sequence& indel, indels)
			{
				if (indel[0]=='+') ++c_ins;
				else if (indel[0]=='-') ++c_del;

			}
			output.frequency = std::min(c_ins, c_del);
		}
		else if (variant.ref()=="-")
		{
			output.frequency = indels.count("+" + variant.obs());
		}
		else
		{
			output.frequency = indels.count("-" + variant.ref());
		}

		//we might count more indels than depth because of the window - correct that
		output.frequency = std::min(1.0, output.frequency / output.depth);
	}

	return output;
}

void NGSHelper::getIndels(const FastaFileIndex& reference, BamReader& reader, const Chromosome& chr, int start, int end, QVector<Sequence>& indels, int& depth, double& mapq0_frac)
{
	//init
	indels.clear();
	depth = 0;
	int reads_mapped = 0;
	int reads_mapq0 = 0;

	//restrict region
	int ref_id = ChromosomeInfo::refID(reader, chr);
	bool jump_ok = reader.SetRegion(ref_id, start-1, ref_id, end);
	if (!jump_ok) THROW(FileAccessException, QString::fromStdString(reader.GetErrorString()));

	//iterate through all alignments and create counts
	BamAlignment al;
	while (reader.GetNextAlignmentCore(al))
	{
		//skip low-quality reads
		if (al.IsDuplicate()) continue;
		if (!al.IsProperPair()) continue;
		if (!al.IsPrimaryAlignment()) continue;
		if (!al.IsMapped()) continue;

		reads_mapped += 1;
		if (al.MapQuality==0)
		{
			reads_mapq0 += 1;
			continue;
		}

		//skip reads that do not span the whole region
		if (al.Position+1>start || al.GetEndPosition()<end ) continue;
		++depth;

		//run time optimization: skip reads that do not contain Indels
		bool contains_indels = false;
		for (unsigned int i=0; i<al.CigarData.size(); ++i)
		{
			if (al.CigarData[i].Type=='I' || al.CigarData[i].Type=='D') contains_indels = true;
		}
		if (!contains_indels) continue;

		//load string data
		al.BuildCharData();
		/*
		QTextStream out(stdout);
		out << "pos      : " << chr.str() << " " << QString::number(al.Position + 1) << "-" << QString::number(al.GetEndPosition()) << endl;
		out << "bases o  : " << QString::fromStdString(al.QueryBases) << endl;
		out << "bases a  : " << QString::fromStdString(al.AlignedBases) << endl;
		out << "cigar    : ";
		for (unsigned int i=0; i<al.CigarData.size(); ++i)
			out << QString(QChar(al.CigarData[i].Type)) << al.CigarData[i].Length;
		out << endl;
		*/

		//look up indels
		int read_pos = 0;
		int genome_pos = al.Position+1; //convert to 1-based position
		for (unsigned int i=0; i<al.CigarData.size(); ++i)
		{
			const CigarOp& op = al.CigarData[i];

			//update positions
			if (op.Type=='M')
			{
				genome_pos += op.Length;
				read_pos += op.Length;
			}
			else if(op.Type=='I')
			{
				if (genome_pos>=start && genome_pos<=end)
				{
					indels.append(QByteArray("+") + al.QueryBases.substr(read_pos, op.Length).c_str());
				}
				read_pos += op.Length;
			}
			else if(op.Type=='D')
			{
				if (genome_pos>=start && genome_pos<=end)
				{
					indels.append("-" + reference.seq(chr.str(), genome_pos, op.Length));
				}
				genome_pos += op.Length;
			}
			else if(op.Type=='N') //skipped reference bases (for RNA)
			{
				genome_pos += op.Length;
			}
			else if(op.Type=='S') //soft-clipped (only at the beginning/end)
			{
				read_pos += op.Length;
			}
			else if(op.Type=='H') //hard-clipped (only at the beginning/end)
			{
				//can be irgnored as hard-clipped bases are not considered in the position or sequence
			}
			else
			{
				THROW(Exception, "Unknown CIGAR operation " + QString(QChar(op.Type)) + "!");
			}
		}
	}

	mapq0_frac = (double)reads_mapq0 / reads_mapped;
}

VariantList NGSHelper::getSNPs()
{
	VariantList output;
	output.load("://Resources/hg19_snps.tsv");
	return output;
}

QPair<char, int> NGSHelper::extractBaseByCIGAR(const BamAlignment& al, int pos)
{
	int read_pos = 0;
	int genome_pos = al.Position;
	for (unsigned int i=0; i<al.CigarData.size(); ++i)
	{
		const CigarOp& op = al.CigarData[i];

		//update positions
		if (op.Type=='M')
		{
			genome_pos += op.Length;
			read_pos += op.Length;
		}
		else if(op.Type=='I')
		{
			read_pos += op.Length;
		}
		else if(op.Type=='D')
		{
			genome_pos += op.Length;

			//base is deleted
			if (genome_pos>=pos) return qMakePair('-', 255);
		}
		else if(op.Type=='N') //skipped reference bases (for RNA)
		{
			genome_pos += op.Length;

			//base is skipped
			if (genome_pos>=pos) return qMakePair('~', -1);
		}
		else if(op.Type=='S') //soft-clipped (only at the beginning/end)
		{
			read_pos += op.Length;

			//base is soft-clipped
			//Nb: reads that are mapped in paired end mode and completely soft-clipped (e.g. 7I64S, 71S) keep their original left-most (genomic) position
			if(read_pos>=al.Length)	return qMakePair('~', -1);
		}
		else if(op.Type=='H') //hard-clipped (only at the beginning/end)
		{
			//can be irgnored as hard-clipped bases are not considered in the position or sequence
		}
		else
		{
			THROW(Exception, "Unknown CIGAR operation " + QString(QChar(op.Type)) + "!");
		}

		if (genome_pos>=pos)
		{
			int actual_pos = read_pos - (genome_pos + 1 - pos);
			int quality = (int)(al.Qualities[actual_pos])-33;
			return qMakePair(al.QueryBases[actual_pos], quality);
		}
	}

	THROW(Exception, "Could not find position  " + QString::number(pos) + " in read " + QString::fromStdString(al.QueryBases) + " with start position " + QString::number(al.Position + 1) + "!");
}

void NGSHelper::extractIndelsByCIGAR(QVector<Sequence>& indels, BamAlignment& al, int pos, int indel_window)
{
	//init
	bool use_window = (indel_window!=0);
	int window_start = pos - indel_window;
	int window_end = pos + indel_window;

	//look up indels
	int read_pos = 0;
	int genome_pos = al.Position+1; //convert to 1-based position
	for (unsigned int i=0; i<al.CigarData.size(); ++i)
	{
		const CigarOp& op = al.CigarData[i];

		//update positions
		if (op.Type=='M') //match or mismatch
		{
			genome_pos += op.Length;
			read_pos += op.Length;
		}
		else if(op.Type=='I') //insert
		{
			if ((!use_window && genome_pos==pos) || (use_window && genome_pos>=window_start && genome_pos<=window_end))
			{
				al.BuildCharData();
				indels.append(QByteArray("+") + al.QueryBases.substr(read_pos, op.Length).c_str());
			}

			read_pos += op.Length;
		}
		else if(op.Type=='D') //deletion
		{
			if ((!use_window && genome_pos==pos) || (use_window && genome_pos>=window_start && genome_pos<=window_end))
			{
				indels.append("-" + QByteArray::number(op.Length));
			}
			genome_pos += op.Length;
		}
		else if(op.Type=='N') //skipped reference bases (for RNA)
		{
			genome_pos += op.Length;
		}
		else if(op.Type=='S') //soft-clipped (only at the beginning/end)
		{
			read_pos += op.Length;
		}
		else if(op.Type=='H') //hard-clipped (only at the beginning/end)
		{
			//can be irgnored as hard-clipped bases are not considered in the position or sequence
		}
		else
		{
			THROW(Exception, "Unknown CIGAR operation " + QString(QChar(op.Type)) + "!");
		}

		//abort if we are behind the indel position
		if ((!use_window && genome_pos>pos) || (use_window && genome_pos>window_end)) break;
	}
}

QString NGSHelper::Cigar2QString(std::vector<CigarOp> Cigar)
{
	QString cigar_string = "";
	for(unsigned int i=0; i<Cigar.size(); ++i)
	{
		CigarOp co = Cigar[i];
		cigar_string = cigar_string + QString::number(co.Length) + QString(co.Type);
	}

	return cigar_string;
}

void NGSHelper::softClipAlignment(BamAlignment& al, int start_ref_pos, int end_ref_pos)
{
	std::vector<CigarOp> old_CIGAR = al.CigarData;
	QString cigar_string = Cigar2QString(old_CIGAR);
	al.AddTag("BS", "Z",cigar_string.toStdString());

	//check preconditions
	if(start_ref_pos > end_ref_pos)	THROW(ToolFailedException, "End position is smaller than start position.");
	if(start_ref_pos < al.Position+1 || start_ref_pos > al.GetEndPosition())	THROW(ToolFailedException, "Start position not within alignment.");
	if(end_ref_pos < al.Position+1 || start_ref_pos > al.GetEndPosition())	THROW(ToolFailedException, "Start position not within alignment.");
	for(unsigned int i=0;i<old_CIGAR.size(); ++i)
	{
		if(old_CIGAR[i].Type!='D' && old_CIGAR[i].Type!='S' && old_CIGAR[i].Type!='M' && old_CIGAR[i].Type!='I')	THROW(ToolFailedException, "Unsupported CIGAR type "+old_CIGAR[i].Type);	//check for supported CIGAR types
	}

	//generate CIGAR char matrix from CIGAR
	std::vector< std::pair<char,char> > matrix;
	for (unsigned int i=0; i<old_CIGAR.size(); ++i)
	{
		//qDebug() << QString::fromStdString(al.Name) <<  old_CIGAR[i].Type;
		for(unsigned int j=0; j<old_CIGAR[i].Length; ++j)
		{
			matrix.push_back(std::make_pair(old_CIGAR[i].Type,old_CIGAR[i].Type));
			//qDebug() << " " << j;
		}
	}

	//soft clip bases in matrix according to given ref_positions
	unsigned int j = 0;
	int current_ref_pos = al.Position+1;	//position is 0-based!
	while(current_ref_pos<=al.GetEndPosition())
	{
		if(j>=matrix.size())	THROW(ToolFailedException, "Index out of boundary!");

		if(current_ref_pos>=start_ref_pos && current_ref_pos<=end_ref_pos)
		{
			matrix[j].second = 'S';
		}

		if(matrix[j].first=='D' || matrix[j].first=='M')	++current_ref_pos;

		//qDebug() << QString::number(j) << matrix[j].first << "=>" << matrix[j].second;
		++j;
	}

	//summarize chars within matrix; generate new CIGAR string
	std::vector<CigarOp> new_CIGAR;
	char tmp_char = 0;
	int tmp_count = 0;
	for(unsigned int i=0; i<matrix.size(); ++i)
	{
		//qDebug() << matrix[i].first << " (" << matrix[i].second << ")" << tmp_count;
		if(matrix[i].first=='D' && matrix[i].second=='S')	//skip soft-clipped deletions
		{
			//qDebug() << " soft-clipped deletion";
			continue;
		}

		if(matrix[i].second!=tmp_char)
		{
			if(tmp_char!=0)
			{
				//
				CigarOp co;
				co.Type = tmp_char;
				co.Length = tmp_count;
				new_CIGAR.insert(new_CIGAR.end(), co);
			}

			//
			tmp_char = matrix[i].second;
			tmp_count = 0;
		}
		++tmp_count;
	}
	CigarOp co;
	co.Type = tmp_char;
	co.Length = tmp_count;
	new_CIGAR.insert(new_CIGAR.end(), co);

	//clean up cigar string;
	for(unsigned int i=1; i<new_CIGAR.size(); ++i)
	{
		bool redo = false;

		//1. remove deleted bases around soft-clipped bases
		if(new_CIGAR[i-1].Type=='S' && new_CIGAR[i].Type=='D')
		{
			new_CIGAR.erase(new_CIGAR.begin()+i);
			redo = true;
		}
		else if(new_CIGAR[i-1].Type=='D' && new_CIGAR[i].Type=='S')
		{
			new_CIGAR.erase(new_CIGAR.begin()+(i-1));
			redo  =true;
		}
		//2. remove inserted bases around soft-clipped bases
		else if(new_CIGAR[i-1].Type=='S' && new_CIGAR[i].Type=='I')
		{
			new_CIGAR[i-1].Length += new_CIGAR[i].Length;
			new_CIGAR.erase(new_CIGAR.begin()+i);
			redo  =true;
		}
		else if(new_CIGAR[i-1].Type=='I' && new_CIGAR[i].Type=='S')
		{
			new_CIGAR[i].Length += new_CIGAR[i-1].Length;
			new_CIGAR.erase(new_CIGAR.begin()+(i-1));
			redo  =true;
		}

		if(redo)	--i;
	}

	//correct left-most position if first bases are soft-clipped, consider bases that were already softclipped previously
	if(matrix[0].second=='S')
	{
		unsigned int i = 0;
		int offset = 0;
		while(i<matrix.size() && matrix[i].second=='S')
		{
			if(matrix[i].first=='M' || matrix[i].first=='D')
			{
				++offset;
			}
			++i;
		}
		al.Position = al.Position + offset;
	}

	al.CigarData.assign(new_CIGAR.begin(),new_CIGAR.end());
}

QByteArray NGSHelper::changeSeq(const QByteArray& seq, bool rev, bool comp)
{
	QByteArray output(seq);

	if (rev)
	{
		std::reverse(output.begin(), output.end());
	}

	if (comp)
	{
		for (int i=0; i<output.count(); ++i)
		{
			switch(output.at(i))
			{
				case 'A':
					output[i] = 'T';
					break;
				case 'C':
					output[i] = 'G';
					break;
				case 'T':
					output[i] = 'A';
					break;
				case 'G':
					output[i] = 'C';
					break;
				case 'N':
					output[i] = 'N';
					break;
				default:
					THROW(ProgrammingException, "Could not convert base " + QString(seq.at(i)) + " to complement!");
			}
		}
	}

	return output;
}

char NGSHelper::complement(char base)
{
	switch(base)
	{
		case 'A':
			return 'T';
		case 'C':
			return 'G';
		case 'T':
			return 'A';
		case 'G':
			return 'C';
		case 'N':
			return 'N';
		default:
			THROW(ProgrammingException, "Could not convert base " + QString(base) + " to complement!");
	}
}
