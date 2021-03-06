#include "GenesToRegionsDialog.h"
#include "ui_GenesToRegionsDialog.h"
#include "NGSD.h"
#include "Exceptions.h"
#include "Helper.h"
#include <QClipboard>
#include <QFileDialog>

GenesToRegionsDialog::GenesToRegionsDialog(QWidget *parent) :
	QDialog(parent),
	ui(new Ui::GenesToRegionsDialog)
{
	ui->setupUi(this);
	connect(ui->convert_btn, SIGNAL(pressed()), this, SLOT(convertGenesToRegions()));
	connect(ui->clip_btn, SIGNAL(pressed()), this, SLOT(copyRegionsToClipboard()));
	connect(ui->store_btn, SIGNAL(pressed()), this, SLOT(storeRegionsAsBED()));
}

GenesToRegionsDialog::~GenesToRegionsDialog()
{
	delete ui;
}

void GenesToRegionsDialog::convertGenesToRegions()
{
	QString text = ui->genes->toPlainText();
	if (text.trimmed()=="")
	{
		ui->regions->clear();
		return;
	}

	//convert input with tabs to plain gene list
	QStringList genes = text.split("\n");
	for (int i=0; i<genes.count(); ++i)
	{
		QString gene = genes[i];
		int pos = gene.indexOf('\t');
		if (pos!=-1) gene = gene.left(pos);
		genes[i] = gene.trimmed();
	}

	//convert gene list to regions (BED)
	QString source = ui->source->currentText().toLower();
	QString mode = ui->mode->currentText().toLower();
	NGSD db;
	QString messages;
	QTextStream stream(&messages);
	regions = db.genesToRegions(genes, source, mode, &stream);
	regions.extend(ui->expand->value());

	//set output
	ui->regions->setPlainText(messages);
	ui->regions->appendPlainText(regions.toText());

	//scroll to top
	ui->regions->moveCursor(QTextCursor::Start);
	ui->regions->ensureCursorVisible();
}

void GenesToRegionsDialog::copyRegionsToClipboard()
{
	QApplication::clipboard()->setText(ui->regions->toPlainText());
}

void GenesToRegionsDialog::storeRegionsAsBED()
{
	QString filename = QFileDialog::getSaveFileName(this, "Store regions as BED file", "", "BED files (*.bed);;All files (*.*)");
	if (filename.isEmpty()) return;

	regions.store(filename);
}
