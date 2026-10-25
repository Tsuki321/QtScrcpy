#ifndef QYUVOPENGLWIDGET_H
#define QYUVOPENGLWIDGET_H
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>

class QYUVOpenGLWidget
    : public QOpenGLWidget
    , protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit QYUVOpenGLWidget(QWidget *parent = nullptr);
    virtual ~QYUVOpenGLWidget() override;

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

    void setFrameSize(const QSize &frameSize);
    const QSize &frameSize();
    void updateTextures(quint8 *dataY, quint8 *dataU, quint8 *dataV, quint32 linesizeY, quint32 linesizeU, quint32 linesizeV);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;

private:
    void initShader();
    void initTextures();
    void deInitTextures();
    void initPBOs();
    void deInitPBOs();
    void updateTexture(GLuint texture, quint32 textureType, quint8 *pixels, quint32 stride);

private:
    QSize m_frameSize = { -1, -1 };
    bool m_needUpdate = false;
    bool m_textureInited = false;

    QOpenGLBuffer m_vbo;
    QOpenGLShaderProgram m_shaderProgram;

    GLuint m_texture[3] = { 0 };

    GLuint m_pbo[3][2] = {{ 0 }};
    int m_pboIndex = 0;
    bool m_pboInited = false;
    quint32 m_pboStride[3] = { 0 };
};

#endif // QYUVOPENGLWIDGET_H
