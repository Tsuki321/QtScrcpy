#include <QCoreApplication>
#include <QOpenGLTexture>
#include <QSurfaceFormat>
#include <QOpenGLContext>
#include <QVector2D>
#include <cstring>

#include "qyuvopenglwidget.h"

// 存储顶点坐标和纹理坐标
// 存在一起缓存在vbo
// 使用glVertexAttribPointer指定访问方式即可
static const GLfloat coordinate[] = {
    // 顶点坐标，存储4个xyz坐标
    // 坐标范围为[-1,1],中心点为 0,0
    // 二维图像z始终为0
    // GL_TRIANGLE_STRIP的绘制方式：
    // 使用前3个坐标绘制一个三角形，使用后三个坐标绘制一个三角形，正好为一个矩形
    // x     y     z
    -1.0f,
    -1.0f,
    0.0f,
    1.0f,
    -1.0f,
    0.0f,
    -1.0f,
    1.0f,
    0.0f,
    1.0f,
    1.0f,
    0.0f,

    // 纹理坐标，存储4个xy坐标
    // 坐标范围为[0,1],左下角为 0,0
    0.0f,
    1.0f,
    1.0f,
    1.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f
};

// 顶点着色器
static const QString s_vertShader = R"(
    attribute vec3 vertexIn;    // xyz顶点坐标
    attribute vec2 textureIn;   // xy纹理坐标
    varying vec2 textureOut;    // 传递给片段着色器的纹理坐标
    void main(void)
    {
        gl_Position = vec4(vertexIn, 1.0);  // 1.0表示vertexIn是一个顶点位置
        textureOut = textureIn; // 纹理坐标直接传递给片段着色器
    }
)";

// 片段着色器
static QString s_fragShader = R"(
    varying vec2 textureOut;        // 由顶点着色器传递过来的纹理坐标
    uniform vec2 texelSize;
    uniform float sharpness;
    uniform sampler2D textureY;     // uniform 纹理单元，利用纹理单元可以使用多个纹理
    uniform sampler2D textureU;     // sampler2D是2D采样器
    uniform sampler2D textureV;     // 声明yuv三个纹理单元
    void main(void)
    {
        vec3 yuv;
        vec3 rgb;

        // SDL2 BT709_SHADER_CONSTANTS
        // https://github.com/spurious/SDL-mirror/blob/4ddd4c445aa059bb127e101b74a8c5b59257fbe2/src/render/opengl/SDL_shaders_gl.c#L102
        const vec3 Rcoeff = vec3(1.1644,  0.000,  1.7927);
        const vec3 Gcoeff = vec3(1.1644, -0.2132, -0.5329);
        const vec3 Bcoeff = vec3(1.1644,  2.1124,  0.000);

        // 根据指定的纹理textureY和坐标textureOut来采样
        yuv.x = texture2D(textureY, textureOut).r;
        if (sharpness > 0.0) {
            float n = texture2D(textureY, textureOut - vec2(0.0, texelSize.y)).r;
            float s = texture2D(textureY, textureOut + vec2(0.0, texelSize.y)).r;
            float w = texture2D(textureY, textureOut - vec2(texelSize.x, 0.0)).r;
            float e = texture2D(textureY, textureOut + vec2(texelSize.x, 0.0)).r;
            float lo = min(yuv.x, min(min(n, s), min(w, e)));
            float hi = max(yuv.x, max(max(n, s), max(w, e)));
            yuv.x = clamp(yuv.x + sharpness * (yuv.x - 0.25 * (n + s + w + e)), lo, hi);
        }

        yuv.y = texture2D(textureU, textureOut).r - 0.5;
        yuv.z = texture2D(textureV, textureOut).r - 0.5;

        // 采样完转为rgb
        // 减少一些亮度
        yuv.x = yuv.x - 0.0625;
        rgb.r = dot(yuv, Rcoeff);
        rgb.g = dot(yuv, Gcoeff);
        rgb.b = dot(yuv, Bcoeff);
        // 输出颜色值
        gl_FragColor = vec4(rgb, 1.0);
    }
)";

QYUVOpenGLWidget::QYUVOpenGLWidget(QWidget *parent) : QOpenGLWidget(parent)
{
    /*
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setColorSpace(QSurfaceFormat::sRGBColorSpace);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    format.setMajorVersion(3);
    format.setMinorVersion(2);
    QSurfaceFormat::setDefaultFormat(format);
    */
}

QYUVOpenGLWidget::~QYUVOpenGLWidget()
{
    makeCurrent();
    m_vbo.destroy();
    deInitTextures();
    doneCurrent();
}

QSize QYUVOpenGLWidget::minimumSizeHint() const
{
    return QSize(50, 50);
}

QSize QYUVOpenGLWidget::sizeHint() const
{
    return size();
}

void QYUVOpenGLWidget::setFrameSize(const QSize &frameSize)
{
    if (m_frameSize != frameSize) {
        m_frameSize = frameSize;
        m_needUpdate = true;
    }
}

const QSize &QYUVOpenGLWidget::frameSize()
{
    return m_frameSize;
}

void QYUVOpenGLWidget::updateTextures(quint8 *dataY, quint8 *dataU, quint8 *dataV, quint32 linesizeY, quint32 linesizeU, quint32 linesizeV)
{
    if (!isValid() || m_frameSize.isEmpty() || !dataY || !dataU || !dataV
            || linesizeY < static_cast<quint32>(m_frameSize.width())
            || linesizeU < static_cast<quint32>((m_frameSize.width() + 1) / 2)
            || linesizeV < static_cast<quint32>((m_frameSize.width() + 1) / 2)) {
        return;
    }
    makeCurrent();
    if (m_needUpdate || !m_textureInited) {
        deInitTextures();
        initTextures();
        m_needUpdate = false;
    }
    updateTexture(m_texture[0], 0, dataY, linesizeY);
    updateTexture(m_texture[1], 1, dataU, linesizeU);
    updateTexture(m_texture[2], 2, dataV, linesizeV);
    doneCurrent();
    update();
}

void QYUVOpenGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glDisable(GL_DEPTH_TEST);

    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(coordinate, sizeof(coordinate));
    initShader();
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
}

void QYUVOpenGLWidget::paintGL()
{
    m_shaderProgram.bind();

    glClear(GL_COLOR_BUFFER_BIT);
    m_vbo.bind();
    m_shaderProgram.setAttributeBuffer("vertexIn", GL_FLOAT, 0, 3, 3 * sizeof(GLfloat));
    m_shaderProgram.enableAttributeArray("vertexIn");
    m_shaderProgram.setAttributeBuffer("textureIn", GL_FLOAT, 12 * sizeof(GLfloat), 2, 2 * sizeof(GLfloat));
    m_shaderProgram.enableAttributeArray("textureIn");

    if (m_textureInited) {
        const bool upscale = width() * devicePixelRatioF() > m_frameSize.width()
                || height() * devicePixelRatioF() > m_frameSize.height();
        m_shaderProgram.setUniformValue("texelSize", QVector2D(1.0f / m_frameSize.width(), 1.0f / m_frameSize.height()));
        m_shaderProgram.setUniformValue("sharpness", m_sharpening && upscale ? 0.35f : 0.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texture[0]);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texture[1]);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_texture[2]);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    m_shaderProgram.release();
}

void QYUVOpenGLWidget::resizeGL(int width, int height)
{
    glViewport(0, 0, width, height);
}

void QYUVOpenGLWidget::initShader()
{
    // opengles的float、int等要手动指定精度
    if (QCoreApplication::testAttribute(Qt::AA_UseOpenGLES)) {
        s_fragShader.prepend(R"(
                             precision mediump int;
                             precision mediump float;
                             )");
    }
    m_shaderProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, s_vertShader);
    m_shaderProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, s_fragShader);
    m_shaderProgram.link();
    m_shaderProgram.bind();

    // 指定顶点坐标在vbo中的访问方式
    // 参数解释：顶点坐标在shader中的参数名称，顶点坐标为float，起始偏移为0，顶点坐标类型为vec3，步幅为3个float
    m_shaderProgram.setAttributeBuffer("vertexIn", GL_FLOAT, 0, 3, 3 * sizeof(float));
    // 启用顶点属性
    m_shaderProgram.enableAttributeArray("vertexIn");

    // 指定纹理坐标在vbo中的访问方式
    // 参数解释：纹理坐标在shader中的参数名称，纹理坐标为float，起始偏移为12个float（跳过前面存储的12个顶点坐标），纹理坐标类型为vec2，步幅为2个float
    m_shaderProgram.setAttributeBuffer("textureIn", GL_FLOAT, 12 * sizeof(float), 2, 2 * sizeof(float));
    m_shaderProgram.enableAttributeArray("textureIn");

    // 关联片段着色器中的纹理单元和opengl中的纹理单元（opengl一般提供16个纹理单元）
    m_shaderProgram.setUniformValue("textureY", 0);
    m_shaderProgram.setUniformValue("textureU", 1);
    m_shaderProgram.setUniformValue("textureV", 2);
}

void QYUVOpenGLWidget::initTextures()
{
    // 创建纹理
    glGenTextures(1, &m_texture[0]);
    glBindTexture(GL_TEXTURE_2D, m_texture[0]);
    // 设置纹理缩放时的策略
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // 设置st方向上纹理超出坐标时的显示策略
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, m_frameSize.width(), m_frameSize.height(), 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &m_texture[1]);
    glBindTexture(GL_TEXTURE_2D, m_texture[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, (m_frameSize.width() + 1) / 2, (m_frameSize.height() + 1) / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &m_texture[2]);
    glBindTexture(GL_TEXTURE_2D, m_texture[2]);
    // 设置纹理缩放时的策略
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // 设置st方向上纹理超出坐标时的显示策略
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, (m_frameSize.width() + 1) / 2, (m_frameSize.height() + 1) / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

    m_textureInited = true;
}

void QYUVOpenGLWidget::deInitTextures()
{
    if (QOpenGLFunctions::isInitialized(QOpenGLFunctions::d_ptr)) {
        glDeleteTextures(3, m_texture);
    }

    memset(m_texture, 0, sizeof(m_texture));
    m_textureInited = false;
}

void QYUVOpenGLWidget::updateTexture(GLuint texture, quint32 textureType, quint8 *pixels, quint32 stride)
{
    if (!pixels) {
        return;
    }
    const QSize plane = textureType == 0 ? m_frameSize : QSize((m_frameSize.width() + 1) / 2, (m_frameSize.height() + 1) / 2);
    if (stride < static_cast<quint32>(plane.width())) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // Desktop GL and GLES 3 support padded FFmpeg planes in a single upload.
    // GLES 2 uses tightly packed scratch memory only when padding is present.
    const bool rowLength = !context()->isOpenGLES() || context()->format().majorVersion() >= 3;
    if (rowLength) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(stride));
    } else if (stride != static_cast<quint32>(plane.width())) {
        m_packedPlane.resize(plane.width() * plane.height());
        for (int y = 0; y < plane.height(); ++y) {
            std::memcpy(m_packedPlane.data() + y * plane.width(), pixels + y * stride,
                        static_cast<size_t>(plane.width()));
        }
        pixels = reinterpret_cast<quint8 *>(m_packedPlane.data());
    }
    // Upload this frame, never the previous (or uninitialized) PBO.
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, plane.width(), plane.height(),
                    GL_LUMINANCE, GL_UNSIGNED_BYTE, pixels);
    if (rowLength) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void QYUVOpenGLWidget::setSharpening(bool enabled)
{
    m_sharpening = enabled;
    update();
}
