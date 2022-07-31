#pragma once

#include <QtQuick/QSGSimpleTextureNode>
#include <QtQuick/QSGTextureProvider>

#include <vulkan/vulkan.hpp>

#include <vector>
#include <memory>

class QQuickWindow;

class CustomTextureNode : public QSGTextureProvider, public QSGSimpleTextureNode {
    Q_OBJECT

public:
    CustomTextureNode( QQuickItem* item );
    ~CustomTextureNode() override;

    QSGTexture* texture() const override;

    void sync();

private slots:
    void render();

private:
    enum Stage { VertexStage, FragmentStage };
    void prepareShader( Stage stage );
    bool buildTexture( const QSize& size );
    void freeTexture();
    bool createRenderPass();
    bool initialize();

    QQuickItem* m_item;
    QQuickWindow* m_window;
    QSize m_size;
    qreal m_device_pixel_ratio;

    std::vector<char> m_vert;
    std::vector<char> m_frag;

    vk::Image m_texture = { nullptr };
    vk::DeviceMemory m_textureMemory = { nullptr };
    vk::Framebuffer m_textureFramebuffer = { nullptr };
    vk::ImageView m_textureView = { nullptr };

    bool m_initialized = false;

    float m_t;

    vk::Instance m_instance;
    vk::PhysicalDevice m_physDev { nullptr };
    vk::Device m_dev { nullptr };
    QVulkanDeviceFunctions* m_devFuncs = nullptr;
    QVulkanFunctions* m_funcs = nullptr;

    vk::Buffer m_vbuf = { nullptr };
    vk::DeviceMemory m_vbufMem = { nullptr };
    vk::Buffer m_ubuf = { nullptr };
    vk::DeviceMemory m_ubufMem = { nullptr };
    vk::DeviceSize m_allocPerUbuf = 0;

    vk::PipelineCache m_pipelineCache = { nullptr };

    vk::PipelineLayout m_pipelineLayout = { nullptr };
    vk::DescriptorSetLayout m_resLayout = { nullptr };
    vk::Pipeline m_pipeline = { nullptr };

    vk::DescriptorPool m_descriptorPool = { nullptr };
    std::vector<vk::DescriptorSet> m_ubufDescriptor = { nullptr };

    vk::RenderPass m_renderPass = { nullptr };
};
