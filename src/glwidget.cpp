/*
 * Copyright (c) 2011 Meltytech, LLC
 * Author: Dan Dennedy <dan@dennedy.org>
 *
 * GL shader based on BSD licensed code from Peter Bengtsson:
 * http://www.fourcc.org/source/YUV420P-OpenGL-GLSLang.c
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QtGui>
#if defined(Q_WS_WIN)
#include "GLee/GLee.h"
#include <GL/glu.h>
#endif
#include <QPalette>
#include <Mlt.h>
#include "glwidget.h"
#if defined(Q_WS_X11)
#include <GL/glu.h>
#elif defined(Q_WS_MAC)
#include <glu.h>
#endif
#if defined(Q_WS_WIN) && !defined(glActiveTexture)
#define glActiveTexture GLeeFuncPtr_glActiveTexture
#endif

#include <QDebug>
#define check_error() { int err = glGetError(); if (err != GL_NO_ERROR) { fprintf(stderr, "GL error 0x%x at %s:%d\n", err, __FILE__, __LINE__); exit(1); } }

using namespace Mlt;

GLWidget::GLWidget(QWidget *parent)
    : QGLWidget(parent)
    , Controller()
    , showFrameSemaphore(3)
    , m_image_width(0)
    , m_image_height(0)
    , m_display_ratio(4.0/3.0)
    , m_glslManager(0)
    , m_fbo(0)
{
    m_texture[0] = m_texture[1] = m_texture[2] = 0;
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    m_glslManager = new Filter(profile(), "glsl.manager");
    if (m_glslManager && !m_glslManager->is_valid()) {
        delete m_glslManager;
        m_glslManager = 0;
    }
    m_renderContext = new QGLWidget(this, this);
    m_renderContext->resize(0, 0);

    mlt_log_set_level(MLT_LOG_VERBOSE);
}

GLWidget::~GLWidget()
{
    makeCurrent();
    if (m_texture[0] && !m_glslManager)
        glDeleteTextures(3, m_texture);
    delete m_fbo;
    delete m_glslManager;
}

QSize GLWidget::minimumSizeHint() const
{
    return QSize(40, 30);
}

QSize GLWidget::sizeHint() const
{
    return QSize(400, 300);
}

void GLWidget::initializeGL()
{
    qDebug() << __FUNCTION__;
    QPalette palette;
    qglClearColor(palette.color(QPalette::Window));
    glShadeModel(GL_FLAT);
    glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_DITHER);
    glDisable(GL_BLEND);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (!m_glslManager) {
        m_shader.addShaderFromSourceCode(QGLShader::Fragment,
        "#extension GL_ARB_texture_rectangle : enable\n"
        "uniform sampler2DRect Ytex, Utex, Vtex;"
        "void main(void) {"
        "  float r, g, b;"
        "  vec4 txl, ux, vx;"
        "  float nx = gl_TexCoord[0].x;"
        "  float ny = gl_TexCoord[0].y;"
        "  float y = texture2DRect(Ytex, vec2(nx, ny)).r;"
        "  float u = texture2DRect(Utex, vec2(nx/2.0, ny/4.0)).r;"
        "  float v = texture2DRect(Vtex, vec2(nx/2.0, ny/4.0)).r;"

        "  y = 1.1643 * (y - 0.0625);"
        "  u = u - 0.5;"
        "  v = v - 0.5;"

        "  r = y + 1.5958  * v;"
        "  g = y - 0.39173 * u - 0.81290 * v;"
        "  b = y + 2.017   * u;"

        "  gl_FragColor = vec4(r, g, b, 1.0);"
        "}");
        m_shader.bind();
    }
}

void GLWidget::resizeGL(int width, int height)
{
    qDebug() << __FUNCTION__;
    double this_aspect = (double) width / height;

    // Special case optimisation to negate odd effect of sample aspect ratio
    // not corresponding exactly with image resolution.
    if ((int) (this_aspect * 1000) == (int) (m_display_ratio * 1000))
    {
        w = width;
        h = height;
    }
    // Use OpenGL to normalise sample aspect ratio
    else if (height * m_display_ratio > width)
    {
        w = width;
        h = width / m_display_ratio;
    }
    else
    {
        w = height * m_display_ratio;
        h = height;
    }
    x = (width - w) / 2;
    y = (height - h) / 2;

    if (isValid()) {
        glViewport(0, 0, width, height);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, width, height, 0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glClear(GL_COLOR_BUFFER_BIT);
    }
}

void GLWidget::resizeEvent(QResizeEvent* event)
{
    resizeGL(event->size().width(), event->size().height());
}

void GLWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (m_texture[0]) {
        if (m_glslManager && m_fbo) {
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB, m_fbo->texture());
            check_error();
        }
        glBegin(GL_QUADS);
            glTexCoord2i(0, 0);
            glVertex2i  (x, y);
            glTexCoord2i(m_image_width, 0);
            glVertex2i  (x + w, y);
            glTexCoord2i(m_image_width, m_image_height);
            glVertex2i  (x + w, y + h);
            glTexCoord2i(0, m_image_height);
            glVertex2i  (x, y + h);
        glEnd();
        check_error();
    }
}

void GLWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        m_dragStart = event->pos();
    emit dragStarted();
}

void GLWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (event->modifiers() == Qt::ShiftModifier && m_producer) {
        emit seekTo(m_producer->get_length() * event->x() / width());
        return;
    }
    if (!(event->buttons() & Qt::LeftButton))
        return;
    if ((event->pos() - m_dragStart).manhattanLength() < QApplication::startDragDistance())
        return;
    QDrag *drag = new QDrag(this);
    QMimeData *mimeData = new QMimeData;
    mimeData->setData("application/mlt+xml", "");
    drag->setMimeData(mimeData);
    drag->exec(Qt::LinkAction);
}

void GLWidget::startGlsl()
{
    qDebug() << __FUNCTION__ << "valid?" << m_renderContext->isValid();
    if (m_glslManager && m_renderContext && m_renderContext->isValid()) {
        m_renderContext->makeCurrent();
        m_glslManager->fire_event("init glsl");
    }
}

static void onThreadStarted(mlt_properties owner, GLWidget* self)
{
    self->startGlsl();
}

void GLWidget::showFrame(Mlt::QFrame frame)
{
    if (frame.frame()->get_int("rendered")) {
        m_image_width = 0;
        m_image_height = 0;
        mlt_image_format format = mlt_image_yuv420p;
        makeCurrent();
        if (m_glslManager) {
            format = mlt_image_glsl_texture;
            const GLuint* textureId = (GLuint*) frame.frame()->get_image(format, m_image_width, m_image_height);
            m_texture[0] = *textureId;

            if (!m_fbo || m_fbo->width() != m_image_width || m_fbo->height() != m_image_height) {
                delete m_fbo;
                m_fbo = new QGLFramebufferObject(m_image_width, m_image_height, GL_TEXTURE_RECTANGLE_ARB);
            }
            glPushAttrib(GL_VIEWPORT_BIT);
            glMatrixMode(GL_PROJECTION);
            glPushMatrix();

            glBindFramebuffer( GL_FRAMEBUFFER, m_fbo->handle());
            check_error();

            glViewport( 0, 0, m_image_width, m_image_height );
            glMatrixMode( GL_PROJECTION );
            glLoadIdentity();
            glOrtho(0.0, m_image_width, 0.0, m_image_height, -1.0, 1.0);
            glMatrixMode( GL_MODELVIEW );
            glLoadIdentity();

            glActiveTexture( GL_TEXTURE0 );
            check_error();
            glBindTexture( GL_TEXTURE_RECTANGLE_ARB, m_texture[0]);
            check_error();

            glBegin( GL_QUADS );
                glTexCoord2i( 0, 0 );                           glVertex2i( 0, 0 );
                glTexCoord2i( 0, m_image_height );              glVertex2i( 0, m_image_height );
                glTexCoord2i( m_image_width, m_image_height );  glVertex2i( m_image_width, m_image_height );
                glTexCoord2i( m_image_width, 0 );               glVertex2i( m_image_width, 0 );
            glEnd();
            check_error();

            glBindFramebuffer( GL_FRAMEBUFFER, 0 );
            check_error();
            glMatrixMode(GL_PROJECTION);
            glPopMatrix();
            glMatrixMode(GL_MODELVIEW);
            glPopAttrib();
        }
        else {
            const uint8_t* image = frame.frame()->get_image(format, m_image_width, m_image_height);

            // Copy each plane of YUV to a texture bound to shader program˙.
            if (m_texture[0])
                glDeleteTextures(3, m_texture);
            glPixelStorei  (GL_UNPACK_ROW_LENGTH, m_image_width);
            glGenTextures  (3, m_texture);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture  (GL_TEXTURE_RECTANGLE_EXT, m_texture[0]);
            m_shader.setUniformValue(m_shader.uniformLocation("Ytex"), 0);
            glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameterf(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D   (GL_TEXTURE_RECTANGLE_EXT, 0, GL_LUMINANCE, m_image_width, m_image_height, 0,
                            GL_LUMINANCE, GL_UNSIGNED_BYTE, image);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture  (GL_TEXTURE_RECTANGLE_EXT, m_texture[1]);
            m_shader.setUniformValue(m_shader.uniformLocation("Utex"), 1);
            glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameterf(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D   (GL_TEXTURE_RECTANGLE_EXT, 0, GL_LUMINANCE, m_image_width/2, m_image_height/4, 0,
                            GL_LUMINANCE, GL_UNSIGNED_BYTE, image + m_image_width * m_image_height);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture  (GL_TEXTURE_RECTANGLE_EXT, m_texture[2]);
            m_shader.setUniformValue(m_shader.uniformLocation("Vtex"), 2);
            glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameterf(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D   (GL_TEXTURE_RECTANGLE_EXT, 0, GL_LUMINANCE, m_image_width/2, m_image_height/4, 0,
                            GL_LUMINANCE, GL_UNSIGNED_BYTE, image + m_image_width * m_image_height + m_image_width/2 * m_image_height/2);
        }
        glDraw();
    }
    showFrameSemaphore.release();
}

int GLWidget::open(Mlt::Producer* producer, bool isMulti)
{
    int error = Controller::open(producer, isMulti);

    if (!error) {
        bool reconnect = !m_consumer || !m_consumer->is_valid();
        error = reconfigure(isMulti);
        if (!error) {
            if (reconnect)
                connect(this, SIGNAL(frameReceived(Mlt::QFrame)),
                        this, SLOT(showFrame(Mlt::QFrame)), Qt::UniqueConnection);
            resizeGL(width(), height());
        }
    }
    return error;
}

int GLWidget::reconfigure(bool isMulti)
{
    int error = 0;

    // use SDL for audio, OpenGL for video
    QString serviceName = property("mlt_service").toString();
    if (!m_consumer || !m_consumer->is_valid()) {
        if (serviceName.isEmpty()) {
            m_consumer = new Mlt::FilteredConsumer(profile(), "sdl_audio");
            if (m_consumer->is_valid())
                serviceName = "sdl_audio";
            else
                serviceName = "rtaudio";
            delete m_consumer;
        }
        if (isMulti)
            m_consumer = new Mlt::FilteredConsumer(profile(), "multi");
        else
            m_consumer = new Mlt::FilteredConsumer(profile(), serviceName.toAscii().constData());
    }
    if (m_consumer->is_valid()) {
        // Connect the producer to the consumer - tell it to "run" later
        m_consumer->connect(*m_producer);
        // Make an event handler for when a frame's image should be displayed
        m_consumer->listen("consumer-frame-show", this, (mlt_listener) on_frame_show);
        m_consumer->set("real_time", property("realtime").toBool()? 1 : -1);
        m_display_ratio = profile().dar();

        if (isMulti) {
            m_consumer->set("terminate_on_pause", 0);
            m_consumer->set("0", serviceName.toAscii().constData());
            if (serviceName == "sdl_audio")
#ifdef Q_WS_WIN
                m_consumer->set("0.audio_buffer", 2048);
#else
                m_consumer->set("0.audio_buffer", 512);
#endif
            if (!profile().progressive())
                m_consumer->set("0.progressive", property("progressive").toBool());
            m_consumer->set("0.rescale", property("rescale").toString().toAscii().constData());
            m_consumer->set("0.deinterlace_method", property("deinterlace_method").toString().toAscii().constData());
            m_consumer->set("0.buffer", 1);
        }
        else {
            if (serviceName == "sdl_audio")
#ifdef Q_WS_WIN
                m_consumer->set("audio_buffer", 2048);
#else
                m_consumer->set("audio_buffer", 512);
#endif
            if (!profile().progressive())
                m_consumer->set("progressive", property("progressive").toBool());
            m_consumer->set("rescale", property("rescale").toString().toAscii().constData());
            m_consumer->set("deinterlace_method", property("deinterlace_method").toString().toAscii().constData());
            m_consumer->set("buffer", 1);
            m_consumer->set("scrub_audio", 1);
            m_consumer->listen("consumer-thread-started", this, (mlt_listener) onThreadStarted);
            // tell the render thread that we will be fetching yuv420p
//            m_consumer->set("mlt_image_format", "yuv420p");
            if (m_glslManager)
                m_consumer->set("mlt_image_format", "glsl");

            Mlt::Filter* filter = new Mlt::Filter(profile(), "audiolevel");
            if (filter->is_valid())
                m_consumer->attach(*filter);
            delete filter;
        }
    }
    else {
        // Cleanup on error
        error = 2;
        Controller::closeConsumer();
        Controller::close();
    }
    return error;
}

// MLT consumer-frame-show event handler
void GLWidget::on_frame_show(mlt_consumer, void* self, mlt_frame frame_ptr)
{
    GLWidget* widget = static_cast<GLWidget*>(self);
    if (widget->showFrameSemaphore.tryAcquire()) {
        Frame frame(frame_ptr);
        emit widget->frameReceived(Mlt::QFrame(frame));
    }
}
