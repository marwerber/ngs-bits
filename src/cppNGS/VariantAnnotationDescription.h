#ifndef VARIANTANNOTATIONDESCRIPTION_H
#define VARIANTANNOTATIONDESCRIPTION_H

#include "cppNGS_global.h"
#include <QString>
#include <QHash>
#include <QSharedPointer>

///stores properties of a annotation.

class CPPNGSSHARED_EXPORT VariantAnnotationDescription
{
public:
	///Types (for VCF).
	enum AnnotationType {INTEGER,FLOAT,FLAG,CHARACTER,STRING};

	///Constructor.
	VariantAnnotationDescription();
	VariantAnnotationDescription(const QString& name, const QString& description, AnnotationType type=STRING, bool sample_specific=false, QString number="1", bool print=true);

	///==Operator (two different INFO or FORMAT annotation can't have same ID in vcf)
	bool operator==(VariantAnnotationDescription b)
	{
		return ((this->name_==b.name_)&&(this->sample_specific_==b.sample_specific_));
	}
	///Returns the name of the annotation.
	const QString& name() const
	{
		return name_;
	}
	///Sets the name of the annotation.
	void setName(const QString& name)
	{
		name_ = name;
	}
	///Returns the description of the annotation.
	const QString& description() const
	{
		return description_;
	}
	///Sets the description of the annotation.
	void setDescription(const QString& description)
	{
		description_ = description;
	}

	///Returns the type of the annotation.
	AnnotationType type() const
	{
		return type_;
	}
	///Sets the type of the annotation.
	void setType(AnnotationType type)
	{
		type_ = type;
	}
	///Returns if the annotion is sample-specific (for VCF).
	bool sampleSpecific() const
	{
		return sample_specific_;
	}
	///Sets if the annotion is sample-specific (for VCF).
	void setSampleSpecific(bool sample_specific)
	{
		sample_specific_ = sample_specific;
	}
	///Returns the number of values of the annotation (for VCF).
	const QString& number() const
	{
		return number_;
	}
	///Sets number of values of the annotation (for VCF).
	void setNumber(const QString& number)
	{
		number_=number;
	}

	///Returns the print value of the annotation description (for VCF).
	bool print() const
	{
		return print_;
	}

protected:
	///Name of the annotation (nearly unique, sample-specific and -independent annotations may have the same name).
	QString name_;
	///Description of the annotation.
	QString description_;
	///The annotation type (see above)
	AnnotationType type_;
	///Information whether the annotation value is sample specific (e.g. read depth) or not (e.g. hapmap2 membership).
	bool sample_specific_;
	///The number of values the annotation consists of, may be positive int (including "0"), ".", "A" or "G".
	QString number_;
	///Print this description to output
	bool print_;
};

class CPPNGSSHARED_EXPORT VariantAnnotationHeader
{
public:
	VariantAnnotationHeader(const QString& name);
	VariantAnnotationHeader(const QString& name, const QString& sample_id);

	///==Operator (two different INFO or FORMAT annotation can't have same ID in vcf)
	bool operator==(VariantAnnotationHeader b)
	{
		return ((this->name_==b.name_)&&(this->sample_id_==b.sample_id_));
	}

	const QString& sampleID() const
	{
		return sample_id_;
	}

	void setSampleID(const QString& sample_id)
	{
		sample_id_ = sample_id;
	}

	const QString& name() const
	{
		return name_;
	}

	void setName(const QString& name)
	{
		name_=name;
	}

	const VariantAnnotationDescription& description() const
	{
		return *description_;
	}

	void setDescription(VariantAnnotationDescription& description)
	{
		description_ = QSharedPointer<VariantAnnotationDescription>(&description);
	}

protected:
	QString name_;
	QString sample_id_;
	QSharedPointer<VariantAnnotationDescription> description_;

};

#endif // VARIANTANNOTATIONDESCRIPTION_H
