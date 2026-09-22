#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <QOpenGLWidget>
#include <QOpenGLFramebufferObject>
#include <QOpenGLTextureBlitter>
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
	// Emitted on the delegate's (GUI) thread with the newest frame only.
	void frameArrived(com_ptr<IDeckLinkVideoFrame> frame);

private:
	void deliverLatestFrame();

	std::atomic<ULONG> m_refCount;

	// DrawFrame (DeckLink thread) only keeps the newest frame; at most one delivery is queued to
	// the GUI thread at a time, so a slow GUI drops stale frames instead of building a backlog.
	std::mutex m_frameMutex;
	com_ptr<IDeckLinkVideoFrame> m_latestFrame;
	std::atomic<bool> m_deliveryPending{false};
};

class DeckLinkOpenGLWidget : public QOpenGLWidget
{
	Q_OBJECT

public:
	DeckLinkOpenGLWidget(QWidget *parent = nullptr);
	~DeckLinkOpenGLWidget() override;

	void setFlipStep(int step);
	com_ptr<DeckLinkOpenGLDelegate> delegate();
	void setSharedDelegate(const com_ptr<DeckLinkOpenGLDelegate> &delegate);
	// Grabs the current view and writes it on a worker thread; the format follows the file
	// extension. Returns false if nothing could be grabbed. onSaved(ok) runs on the GUI thread
	// once the file has actually been written (or failed).
	bool saveSnapshot(const QString &path, std::function<void(bool ok)> onSaved);

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

	// GPU-side flip: the preview is rendered into m_flipFbo and drawn mirrored by m_blitter
	std::unique_ptr<QOpenGLFramebufferObject> m_flipFbo;
	QOpenGLTextureBlitter m_blitter;

	// 🔴 NEW
	bool m_recording = false;
	VideoRecorder *m_videoRecorder = nullptr;
	QString m_inputSource = "SDI";
	bool m_showLabel = true;
};
