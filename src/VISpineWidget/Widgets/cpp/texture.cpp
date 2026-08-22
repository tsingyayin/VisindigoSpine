/******************************************************************************
 * Spine Runtimes Software License
 * Version 2.1
 *
 * Copyright (c) 2013, Esoteric Software
 * All rights reserved.
 *
 * You are granted a perpetual, non-exclusive, non-sublicensable and
 * non-transferable license to install, execute and perform the Spine Runtimes
 * Software (the "Software") solely for internal use. Without the written
 * permission of Esoteric Software (typically granted by licensing Spine), you
 * may not (a) modify, translate, adapt or otherwise create derivative works,
 * improvements of the Software or develop new applications using the Software
 * or (b) remove, delete, alter or obscure any trademarks or any copyright,
 * trademark, patent or other intellectual property or proprietary rights
 * notices on or in the Software, including any copy thereof. Redistributions
 * in binary or source form must include this license and terms.
 *
 * THIS SOFTWARE IS PROVIDED BY ESOTERIC SOFTWARE "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL ESOTERIC SOFTARE BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#include "spinewidget_p.h"
#include <QImage>
#include <QFileInfo>
#include <QDebug>
#include <spine/Extension.h>

namespace Visindigo::Widgets {

/*
    \brief 贴图加载器构造: 复位冻结标记。
*/
AimyTextureLoader::AimyTextureLoader()
{
    gTextureFreezed = false;
}

/*
    \brief 析构: 置冻结标记并释放全部贴图。
*/
AimyTextureLoader::~AimyTextureLoader()
{
    gTextureFreezed = true;
    releaseTextures();
}

/*
    \brief 返回贴图加载器单例。
    \return 全局唯一实例
*/
AimyTextureLoader* AimyTextureLoader::instance()
{
    static AimyTextureLoader instance;
    return &instance;
}

/*
    \brief 加载图集页引用的贴图文件并登记到贴图哈希表。
    \a page 图集页(成功后写入 page.texture)
    \a path 贴图文件路径
*/
void AimyTextureLoader::load(spine::AtlasPage& page, const spine::String& path)
{
    if (gTextureFreezed) {
        qWarning() << "Texture loader has been freezed: recreating new texture loader...";
        AimyTextureLoader::instance();
    }
    QMutexLocker locker(&mutex);
    QString filePath(path.buffer());

    if (textureHash.contains(filePath)) {
        Texture* tex = textureHash.value(filePath);
        page.texture = tex;
        return;
    }

    if (not QFile::exists(filePath)) {
        qWarning() << filePath << " not exists...";
        return;
    }
    Texture* tex = new Texture(filePath);
    tex->image = QImage(filePath);
    if (tex->image.isNull())
        qWarning() << "Failed to load image:" << filePath;

    page.texture = tex;
    textureHash.insert(filePath, tex);
}

/*
    \brief 卸载贴图(spine 回调, 当前为空实现, 资源统一由哈希表管理)。
    \a texture 待卸载贴图指针
*/
void AimyTextureLoader::unload(void* texture)
{
    Q_UNUSED(texture)
}

/*
    \brief 返回贴图对象本身(本实现无需额外 GL 包装)。
    \a texture 输入贴图
    \return 同一贴图对象
*/
Texture* AimyTextureLoader::getGLTexture(Texture* texture)
{
    return texture;
}

/*
    \brief 释放全部贴图(含 GL 纹理), 并清空哈希表。
*/
void AimyTextureLoader::releaseTextures()
{
    QMutexLocker locker(&mutex);
    for (auto it = textureHash.begin(); it != textureHash.end(); ++it)
        delete it.value();
    textureHash.clear();
}

/*
    \brief 失效所有贴图的 GL 纹理(上下文重建时调用), 保留 CPU 侧 QImage。
*/
void AimyTextureLoader::invalidateGlTextures()
{
    QMutexLocker locker(&mutex);
    for (auto it = textureHash.begin(); it != textureHash.end(); ++it) {
        if (it.value()->glTexture) {
            delete it.value()->glTexture;
            it.value()->glTexture = nullptr;
        }
    }
}

/*
    \brief 构造扩展(spine DefaultSpineExtension 子类)。
*/
AimyExtension::AimyExtension(): spine::DefaultSpineExtension()
{
}

/*
    \brief 析构。
*/
AimyExtension::~AimyExtension()
{
}

/*
    \brief 以原生方式读取文件字节(spine 运行时读取 skel/json 等资源用)。
    \a path 文件路径
    \a length 出参: 读取字节数
    \return malloc 分配的缓冲区, 由调用方负责释放
*/
char* AimyExtension::_readFile(const spine::String& path, int* length)
{
    QString filePath(path.buffer());
    *length = 0;
    if (not QFile::exists(filePath))
        return nullptr;
    QFile file(filePath);
    if (not file.open(QIODevice::ReadOnly)) {
        qWarning() << file.errorString();
        return nullptr;
    }
    QByteArray bytes = file.readAll();
    file.close();
    *length = bytes.size();

    char* datas = static_cast<char*>(malloc(bytes.size()));
    memcpy(datas, bytes.data(), *length);

    return datas;
}

} // namespace Visindigo::Widgets

// 该函数声明在全局 spine 命名空间(spine runtime 的扩展入口), 必须在命名空间外定义
spine::SpineExtension* spine::getDefaultExtension() {
    return new Visindigo::Widgets::AimyExtension();
}

