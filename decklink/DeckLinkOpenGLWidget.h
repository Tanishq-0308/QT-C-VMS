#pragma once

#include <atomic>
#include <mutex>
#include <QOpenGLWidget>
#include "com_ptr.h"
#include "DeckLinkAPI.h"
#include "VideoRecorder.hpp"

// Forward declaration (or you can #include "VideoRecorder.hpp" instead)
class VideoRecorder;

class DeckLinkOpenGLDelegate : public QObject, public IDeckLinkScreenPreviewCallback
{
	Q_OBJECT

public:
	DeckLinkOpenGLDelegate();
	virtual ~DeckLinkOpenGLDelegate() = default;

	// IUnknown
	HRESULT QueryInterface(REFIID iid, LPVOID *ppv) override;
	ULONG AddRef() override;
	ULONG Release() override;

	// IDeckLinkScreenPreviewCallback
	HRESULT DrawFrame(IDeckLinkVideoFrame *theFrame) override;

signals:
	void frameArrived(com_ptr<IDeckLinkVideoFrame> frame);

private:
	std::atomic<ULONG> m_refCount;
};

class DeckLinkOpenGLWidget : public QOpenGLWidget
{
	Q_OBJECT

public:
	DeckLinkOpenGLWidget(QWidget *parent = nullptr);
	virtual ~DeckLinkOpenGLWidget() = default;

	void setFlipStep(int step);
	com_ptr<DeckLinkOpenGLDelegate> delegate();
	void setSharedDelegate(const com_ptr<DeckLinkOpenGLDelegate> &delegate);
	bool saveSnapshot(const QString &path);

	// 🔴 NEW: Recording control
	void setRecording(bool recording);
	void setVideoRecorder(VideoRecorder *recorder);
	void setInputSource(const QString &source);
	void setShowLabel(bool show);

	void clear();

protected:
	// QOpenGLWidget
	void initializeGL() override;
	void paintGL() override;
	void resizeGL(int width, int height) override;

private slots:
	void setFrame(com_ptr<IDeckLinkVideoFrame> frame);

private:
	com_ptr<DeckLinkOpenGLDelegate> m_delegate;
	com_ptr<IDeckLinkGLScreenPreviewHelper> m_deckLinkScreenPreviewHelper;
	std::mutex m_mutex;
	int m_flipStep = 0;

	// 🔴 NEW
	bool m_recording = false;
	VideoRecorder *m_videoRecorder = nullptr;
	QString m_inputSource = "SDI";
	bool m_showLabel = true;
};
