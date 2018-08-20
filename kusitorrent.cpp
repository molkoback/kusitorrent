/* kusitorrent.cpp
 * 
 * Copyright (C) 2018 molko <molkoback@gmail.com>
 * This work is free. You can redistribute it and/or modify it under the
 * terms of the Do What The Fuck You Want To Public License, Version 2,
 * as published by Sam Hocevar. See the COPYING file for more details.
 */

/* Qt */
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>

/* libktorrent */
#include <interfaces/queuemanagerinterface.h>
#include <interfaces/serverinterface.h>
#include <peer/authenticationmonitor.h>
#include <util/error.h>
#include <util/fileops.h>
#include <util/functions.h>
#include <util/waitjob.h>
#include <torrent/torrentcontrol.h>
#include <torrent/torrentstats.h>
#include <version.h>

#include <util/log.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <memory>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define APP_NAME "KusiTorrent"
#define APP_VERSION "0.1.0"

#define UPDATE_DELAY 250

class ProgressBar {
private:
	unsigned int width;
	unsigned int barWidth;
	std::unique_ptr<char> barEmpty;
	std::unique_ptr<char> barFull;

public:
	ProgressBar(int width) :
		width(width),
		barWidth(width-7),
		barEmpty(new char[barWidth+1]),
		barFull(new char[barWidth+1])
	{
		char *buf;
		
		buf = this->barEmpty.get();
		memset(buf, '-', this->barWidth);
		buf[this->barWidth] = '\0';
		
		buf = barFull.get();
		memset(buf, '#', this->barWidth);
		buf[this->barWidth] = '\0';
	}
	
	~ProgressBar() {}
	
	void print(unsigned int total, unsigned int progress)
	{
		float ratio = (float)progress / total;
		unsigned int percent = ratio * 100;
		unsigned int count = ratio * this->barWidth;
		
		fprintf(
			stdout,
			"\r[%.*s%.*s] %3d%%",
			count, this->barFull.get(),
			this->barWidth - count, this->barEmpty.get(),
			percent
		);
		if (total == progress)
			fprintf(stdout, "\n");
		fflush(stdout);
	}
};

class KusiTorrent : public QCoreApplication, public bt::QueueManagerInterface {
	Q_OBJECT
private:
	std::unique_ptr<bt::TorrentControl> control;
	std::unique_ptr<QCommandLineParser> parser;
	std::unique_ptr<ProgressBar> prog;
	QTimer timer;
	int updates;
	
	quint16 port;
	QString dlPath;
	QString tmpPath;
	int quiet_f;
	
	QStringList files;
	
	quint16 randint(quint16 min, quint16 max) const
	{
		return min + rand() % (max-min);
	}
	
	quint16 parsePort(QString portstr) const
	{
		bool ok;
		quint16 num;
		num = portstr.toInt(&ok);
		if (!ok)
			throw bt::Error(QString("invalid port '%1'").arg(portstr));
		return num ? num : randint(6881, 6999);
	}
	
	QString parsePath(QString dirstr)
	{
		QDir dir(dirstr);
		QDir root = QDir::root();
		if (!dir.makeAbsolute())
			throw bt::Error(QString("invalid directory '%1'").arg(dirstr));
		if (!dir.exists())
			if (!root.mkpath(dir.path()))
				throw bt::Error(QString("couldn't create directory '%1'").arg(dirstr));
		return dir.path();
	}
	
	QStringList parseFiles(const QStringList &files)
	{
		if (files.isEmpty())
			throw bt::Error(QString("no files given"));
		for (auto &file : files) {
			QFileInfo info(file);
			if (!info.exists() || !info.isFile())
				throw bt::Error(QString("invalid file '%1'").arg(file));
		}
		return files;
	}
	
	void parse_args()
	{
		this->parser->addPositionalArgument("<file>", "Torrent file.");
		this->parser->addOption(QCommandLineOption(
			{"d", "dir"},
			"Download directory.",
			"dir", QDir::currentPath()
		));
		this->parser->addOption(QCommandLineOption(
			{"p", "port"},
			"Port to use.",
			"port", "0"
		));
		this->parser->addOption(QCommandLineOption(
			{"q", "quiet"},
			"Quiet mode."
		));
		this->parser->addHelpOption();
		this->parser->addVersionOption();
		this->parser->process(*this);
		
		this->port = this->parsePort(this->parser->value("port"));
		this->dlPath = this->parsePath(this->parser->value("dir"));
		this->tmpPath = this->parsePath(QStringList(
			{QDir::tempPath(), APP_NAME}
		).join(QDir::separator()));
		
		this->quiet_f = this->parser->isSet("quiet");
		
		this->files = this->parseFiles(this->parser->positionalArguments());
	}
	
	void ktorrent_init()
	{
		if (!bt::InitLibKTorrent())
			throw bt::Error(QString("failed to initialize libktorrent"));
		//bt::InitLog("kusi.log", false, true, false);
		bt::SetClientInfo(
			APP_NAME,
			bt::MAJOR, bt::MINOR, bt::RELEASE, bt::NORMAL,
			"KT"
		);
	}
	
	void progress_init()
	{
		struct winsize ws;
		ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
		this->prog = std::unique_ptr<ProgressBar>(new ProgressBar(ws.ws_col));
	}
	
	void open(const QString &file)
	{
		control->init(
			this,
			bt::LoadFile(file),
			this->tmpPath, this->dlPath
		);
		control->setLoadUrl(QUrl(file));
		control->createFiles();
		control->start();
		connect(
			&this->timer, SIGNAL(timeout()),
			this, SLOT(update())
		);
		this->timer.start(UPDATE_DELAY);
	}

public:
	KusiTorrent(int argc, char *argv[]) :
		QCoreApplication(argc, argv),
		control(new bt::TorrentControl()),
		parser(new QCommandLineParser()),
		updates(0)
	{
		this->setApplicationName(APP_NAME);
		this->setApplicationVersion(APP_VERSION);
		
		srand(time(NULL));
		
		this->parse_args();
		if (!this->quiet_f)
			this->progress_init();
		
		this->ktorrent_init();
		connect(
			this->control.get(), SIGNAL(finished(bt::TorrentInterface *)),
			this, SLOT(finished(bt::TorrentInterface *))
		);
		connect(
			this, SIGNAL(aboutToQuit()),
			this, SLOT(shutdown())
		);
	}
	
	virtual ~KusiTorrent() {}
	
	void start()
	{
		bt::ServerInterface::setPort(this->port);
		if (!bt::Globals::instance().initTCPServer(port))
			throw bt::Error(QString("failed to use port '%1'").arg(this->port));
		this->open(this->files[0]);
	}
	
	virtual bool notify(QObject *obj, QEvent *ev) {
		return QCoreApplication::notify(obj, ev);
	}
	
	virtual bool alreadyLoaded(const bt::SHA1Hash &ih) const
	{
		Q_UNUSED(ih); 
		return false;
	}

	virtual void mergeAnnounceList(const bt::SHA1Hash &ih, const bt::TrackerTier *trk)
	{
		Q_UNUSED(ih);
		Q_UNUSED(trk);
	}
	
public Q_SLOTS:
	void update()
	{
		bt::UpdateCurrentTime();
		bt::AuthenticationMonitor::instance().update();
		this->control->update();
		
		this->updates++;
		if (!this->quiet_f && this->updates % (1000 / UPDATE_DELAY) == 0) {
			bt::TorrentStats stats = this->control->getStats();
			this->prog->print(stats.total_bytes, stats.bytes_downloaded);
		}
	}
	
	void finished(bt::TorrentInterface *tor)
	{
		Q_UNUSED(tor);
		QTimer::singleShot(0, this, SLOT(shutdown()));
	}
	
	void shutdown()
	{
		bt::AuthenticationMonitor::instance().shutdown();
		
		bt::WaitJob *job = new bt::WaitJob(2*UPDATE_DELAY);
		this->control->stop(job);
		if (job->needToWait())
			job->exec();
		job->deleteLater();
		
		bt::Globals::instance().shutdownTCPServer();
		bt::Globals::instance().shutdownUTPServer();
		this->quit();
	}
};

#include "kusitorrent.moc"

static void siginthandler(int signum)
{
	Q_UNUSED(signum);
	qApp->quit();
}

static void perror(const QString &str)
{
	fprintf(stderr, "Error: %s\n", str.toStdString().c_str());
}

int main(int argc, char *argv[])
{
	signal(SIGINT, siginthandler);
	try {
		KusiTorrent app(argc, argv);
		app.start();
		return app.exec();
	}
	catch (bt::Error &err) {
		perror(err.toString());
	}
	catch (std::exception &err) {
		perror(err.what());
	}
	return EXIT_FAILURE;
}
