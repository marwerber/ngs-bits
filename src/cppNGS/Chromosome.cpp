#include "Chromosome.h"
#include "Exceptions.h"
#include <QHash>

Chromosome::Chromosome()
	: str_("")
	, num_(0)
{
}

Chromosome::Chromosome(const QString& chr)
	: str_(chr.trimmed().toLatin1())
	, num_(0)
{
	num_ = numericRepresentation();
}

Chromosome::Chromosome(const QByteArray& chr)
	: str_(chr.trimmed())
	, num_(0)
{
	num_ = numericRepresentation();
}

Chromosome::Chromosome(const char* chr)
	: str_(chr)
	, num_(0)
{
	str_ = str_.trimmed();
	num_ = numericRepresentation();
}

Chromosome::Chromosome(const std::string& chr)
	: str_(chr.c_str())
	, num_(0)
{
	str_ = str_.trimmed();
	num_ = numericRepresentation();
}

QByteArray Chromosome::normalizedStringRepresentation() const
{
	QByteArray tmp = str_;
	tmp = tmp.toUpper();
	if (tmp.startsWith("CHR")) tmp = tmp.mid(3);
	if (tmp=="MT") tmp = "M";
	return tmp;
}

int Chromosome::numericRepresentation() const
{
	//normalize
	QByteArray tmp = normalizedStringRepresentation();

	//handle special cases empty, X,Y and M/MT
	if (tmp=="")
	{
		return 0;
	}
	if (tmp=="X")
	{
		return 1001;
	}
	if (tmp=="Y")
	{
		return 1002;
	}
	if (tmp=="M" || tmp=="MT")
	{
		return 1003;
	}

	//calculate numeric representation
	bool ok = false;
	int value = tmp.toUInt(&ok);
	if (ok && value>=1 && value<=1000)
	{
		return value;
	}
	//other non-numeric chromosome
	static int next_num = 1004;
	static QHash<QByteArray, int> cache;
	if (!cache.contains(tmp))
	{
		cache[tmp] = next_num;
		++next_num;
	}

	return cache[tmp];
}
