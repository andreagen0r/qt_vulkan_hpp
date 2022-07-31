#include "customtexturenode.h"

#include <QtGui/QScreen>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <QFile>
#include <exception>
#include <array>


CustomTextureNode::CustomTextureNode( QQuickItem* item )
    : m_item( item ) {

    m_window = m_item->window();

    connect( m_window, &QQuickWindow::beforeRendering, this, &CustomTextureNode::render );

    connect( m_window, &QQuickWindow::screenChanged, this, [this]() {
        if ( !qFuzzyCompare( m_window->effectiveDevicePixelRatio(), m_device_pixel_ratio ) ) {
            m_item->update();
        }
    } );
}

CustomTextureNode::~CustomTextureNode() {
    m_dev.destroyBuffer( m_vbuf );
    m_dev.destroyBuffer( m_ubuf );
    m_dev.freeMemory( m_ubufMem );
    m_dev.freeMemory( m_ubufMem );

    m_dev.destroyPipelineCache( m_pipelineCache );
    m_dev.destroyPipelineLayout( m_pipelineLayout );
    m_dev.destroyPipeline( m_pipeline );
    m_dev.destroyRenderPass( m_renderPass );
    m_dev.destroyDescriptorSetLayout( m_resLayout );
    m_dev.destroyDescriptorPool( m_descriptorPool );

    delete texture();
    freeTexture();
}

QSGTexture* CustomTextureNode::texture() const {
    return QSGSimpleTextureNode::texture();
}

// clang-format off
const std::vector<float> vertices {
    -1, -1,
     1, -1,
    -1,  1,
     1,  1 };

// clang-format on
const int UBUF_SIZE = 4;

bool CustomTextureNode::buildTexture( const QSize& size ) {

    vk::ImageCreateInfo imageInfo( vk::ImageCreateInfo(
        vk::ImageCreateFlags(), vk::ImageType::e2D, {},
        vk::Extent3D( static_cast<uint32_t>( size.width() ), static_cast<uint32_t>( size.height() ), 1 ), 1U, 1U, vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferSrc,
        vk::SharingMode::eExclusive, 1, nullptr, vk::ImageLayout::eUndefined ) );

    imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eColorAttachment;

    vk::Image image;
    try {
        image = { m_dev.createImage( imageInfo ) };
    } catch ( vk::SystemError err ) {
        qCritical() << "VulkanWrapper: failed to create image! - ", err.what();
        return false;
    }

    m_texture = image;

    vk::MemoryRequirements memReq { m_dev.getImageMemoryRequirements( image ) };

    quint32 memIndex = 0;
    VkPhysicalDeviceMemoryProperties physDevMemProps;

    // FIXME: ????
    m_window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties( m_physDev, &physDevMemProps );
    for ( uint32_t i = 0; i < physDevMemProps.memoryTypeCount; ++i ) {
        if ( !( memReq.memoryTypeBits & ( 1 << i ) ) )
            continue;
        memIndex = i;
    }

    vk::MemoryAllocateInfo allocInfo { memReq.size, memIndex, nullptr };

    try {
        m_textureMemory = m_dev.allocateMemory( allocInfo );

    } catch ( vk::SystemError err ) {
        qWarning() << "Failed to allocate memory for linear image: " << err.what();
        return false;
    }

    try {
        m_dev.bindImageMemory( image, m_textureMemory, 0 );
    } catch ( vk::SystemError err ) {
        qWarning() << "Failed to bind linear image memory: " << err.what();
        return false;
    }

    vk::ImageViewCreateInfo viewInfo(
        vk::ImageViewCreateFlags {}, image, vk::ImageViewType::e2D, imageInfo.format,
        vk::ComponentMapping( vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA ),
        vk::ImageSubresourceRange( vk::ImageAspectFlags {}, 0, 0 ), nullptr );


    try {
        m_textureView = m_dev.createImageView( viewInfo );
    } catch ( vk::SystemError err ) {
        qWarning() << "Failed to create render target image view: " << err.what();
        return false;
    }

    vk::FramebufferCreateInfo fbInfo( vk::FramebufferCreateFlags {}, m_renderPass, 1, &m_textureView, uint32_t( size.width() ),
                                      uint32_t( size.height() ), 1 );

    try {
        m_textureFramebuffer = m_dev.createFramebuffer( fbInfo );

    } catch ( vk::SystemError err ) {
        qWarning() << "Failed to create framebuffer: " << err.what();
        return false;
    }

    return true;
}

void CustomTextureNode::freeTexture() {
    if ( m_texture ) {
        m_dev.destroyFramebuffer( m_textureFramebuffer );
        m_dev.freeMemory( m_textureMemory );
        m_dev.destroyImageView( m_textureView );
        m_dev.destroyImage( m_texture );
    }
}

static inline VkDeviceSize aligned( VkDeviceSize v, VkDeviceSize byteAlign ) {
    return ( v + byteAlign - 1 ) & ~( byteAlign - 1 );
}

bool CustomTextureNode::createRenderPass() {
    const vk::Format vkformat { vk::Format::eR8G8B8A8Unorm };
    const vk::SampleCountFlagBits samples { vk::SampleCountFlagBits::e1 };

    vk::AttachmentDescription colorAttDesc( vk::AttachmentDescriptionFlags {}, vkformat, samples, vk::AttachmentLoadOp::eClear,
                                            vk::AttachmentStoreOp::eDontCare, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                                            vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal );

    const vk::AttachmentReference colorRef( 0, vk::ImageLayout::eColorAttachmentOptimal );

    vk::SubpassDescription subpassDesc( vk::SubpassDescriptionFlags {}, vk::PipelineBindPoint::eGraphics, 1, nullptr, 1, &colorRef, nullptr, nullptr,
                                        0, nullptr );
    vk::RenderPassCreateInfo rpInfo( vk::RenderPassCreateFlags {}, 1, &colorAttDesc, 1, &subpassDesc );

    try {
        m_renderPass = m_dev.createRenderPass( rpInfo );
    } catch ( vk::SystemError err ) {
        qWarning() << "Failed to create renderpass: " << err.what();
        return false;
    }

    return true;
}

bool CustomTextureNode::initialize() {
    const int framesInFlight = m_window->graphicsStateInfo().framesInFlight;
    m_initialized = true;

    QSGRendererInterface* rif = m_window->rendererInterface();
    QVulkanInstance* inst = reinterpret_cast<QVulkanInstance*>( rif->getResource( m_window, QSGRendererInterface::VulkanInstanceResource ) );
    Q_ASSERT( inst && inst->isValid() );

    m_physDev = *static_cast<vk::PhysicalDevice*>( rif->getResource( m_window, QSGRendererInterface::PhysicalDeviceResource ) );
    m_dev = *static_cast<vk::Device*>( rif->getResource( m_window, QSGRendererInterface::DeviceResource ) );
    Q_ASSERT( m_physDev && m_dev );

    //    m_devFuncs = inst->deviceFunctions( m_dev );
    //    m_funcs = inst->functions();
    //    Q_ASSERT( m_devFuncs && m_funcs );

    //    uint32_t extensionCount { 0 };
    //    m_funcs->vkEnumerateInstanceExtensionProperties( nullptr, &extensionCount, nullptr );
    //    qDebug() << u"Extension count:"_qs << extensionCount;

    createRenderPass();

    vk::PhysicalDeviceProperties physDevProps { m_physDev.getProperties() };
    vk::PhysicalDeviceMemoryProperties physDevMemProps { m_physDev.getMemoryProperties() };
    vk::BufferCreateInfo bufferInfo( vk::BufferCreateFlags {}, sizeof( vertices ), vk::BufferUsageFlagBits::eVertexBuffer );

    try {
        m_vbuf = m_dev.createBuffer( bufferInfo );

    } catch ( vk::SystemError err ) {
        qFatal( "Failed to create vertex buffer: %s", err.what() );
        return false;
    }

    vk::MemoryRequirements memReq( m_dev.getBufferMemoryRequirements( m_vbuf ) );
    vk::MemoryAllocateInfo allocInfo( memReq.size );
    uint32_t memTypeIndex = uint32_t( -1 );
    const auto memType = physDevMemProps.memoryTypes;

    for ( uint32_t i = 0; i < physDevMemProps.memoryTypeCount; ++i ) {
        if ( memReq.memoryTypeBits & ( 1 << i ) ) {
            if ( ( memType[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible )
                 && ( memType[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent ) ) {
                memTypeIndex = i;
                break;
            }
        }
    }

    if ( memTypeIndex == uint32_t( -1 ) ) {
        qFatal( "Failed to find host visible and coherent memory type" );
    }

    allocInfo.memoryTypeIndex = memTypeIndex;

    try {
        m_vbufMem = m_dev.allocateMemory( allocInfo );
    } catch ( vk::SystemError err ) {
        qFatal( "Failed to allocate vertex buffer memory of size %u: %s", uint( allocInfo.allocationSize ), err.what() );
    }


    void* p = nullptr;
    try {
        p = m_dev.mapMemory( m_vbufMem, 0, allocInfo.allocationSize );

    } catch ( vk::SystemError err ) {
        qFatal( "Failed to map vertex buffer memory: %s", err.what() );
        return false;
    }


    memcpy( p, vertices.data(), sizeof( vertices ) * vertices.size() );

    try {
        m_dev.unmapMemory( m_vbufMem );
    } catch ( vk::SystemError err ) { qFatal( "Failed to bind vertex buffer memory: %s", err.what() ); }

    m_allocPerUbuf = aligned( UBUF_SIZE, physDevProps.limits.minUniformBufferOffsetAlignment );

    bufferInfo.size = framesInFlight * m_allocPerUbuf;
    bufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;

    try {
        m_ubuf = m_dev.createBuffer( bufferInfo );
    } catch ( vk::SystemError err ) { qFatal( "Failed to create uniform buffer: %s", err.what() ); }

    memReq = m_dev.getBufferMemoryRequirements( m_ubuf );
    memTypeIndex = -1;

    for ( uint32_t i = 0; i < physDevMemProps.memoryTypeCount; ++i ) {
        if ( memReq.memoryTypeBits & ( 1 << i ) ) {
            if ( ( memType[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible )
                 && ( memType[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent ) ) {
                memTypeIndex = i;
                break;
            }
        }
    }
    if ( memTypeIndex == uint32_t( -1 ) ) {
        qFatal( "Failed to find host visible and coherent memory type" );
    }

    allocInfo.allocationSize = qMax( memReq.size, framesInFlight * m_allocPerUbuf );
    allocInfo.memoryTypeIndex = memTypeIndex;

    try {
        m_ubufMem = m_dev.allocateMemory( allocInfo );
    } catch ( vk::SystemError err ) {
        qFatal( "Failed to allocate uniform buffer memory of size %u: %s", uint( allocInfo.allocationSize ), err.what() );
    }

    try {
        m_dev.bindBufferMemory( m_ubuf, m_vbufMem, 0 );
    } catch ( vk::SystemError err ) { qFatal( "Failed to bind uniform buffer memory: %s", err.what() ); }

    // Now onto the pipeline.
    vk::PipelineCacheCreateInfo pipelineCacheInfo( vk::PipelineCacheCreateFlags {} );

    try {
        m_pipelineCache = m_dev.createPipelineCache( pipelineCacheInfo );
    } catch ( vk::SystemError err ) {
        qFatal( "Failed to create pipeline cache: %s", err.what() );
        return false;
    }

    vk::DescriptorSetLayoutBinding descLayoutBinding( 0, vk::DescriptorType::eUniformBufferDynamic, 1,
                                                      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment );

    vk::DescriptorSetLayoutCreateInfo layoutInfo( vk::DescriptorSetLayoutCreateFlags {}, 1, &descLayoutBinding );

    try {
        m_resLayout = m_dev.createDescriptorSetLayout( layoutInfo );
    } catch ( vk::SystemError err ) {
        qFatal( "Failed to create descriptor set layout: %s", err.what() );
        return false;
    }

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo( vk::PipelineLayoutCreateFlags {}, 1, &m_resLayout );

    try {
        m_pipelineLayout = m_dev.createPipelineLayout( pipelineLayoutInfo );
    } catch ( vk::SystemError err ) {
        qWarning( "Failed to create pipeline layout: %s", err.what() );
        return false;
    }

    vk::GraphicsPipelineCreateInfo pipelineInfo;

    vk::ShaderModuleCreateInfo shaderInfo( vk::ShaderModuleCreateFlags {}, m_vert.size(), reinterpret_cast<const quint32*>( m_vert.data() ) );

    vk::ShaderModule vertShaderModule;

    try {
        vertShaderModule = m_dev.createShaderModule( shaderInfo );
    } catch ( vk::SystemError err ) {
        qFatal( "Failed to create vertex shader module: %s", err.what() );
        return false;
    }

    shaderInfo.codeSize = m_frag.size();
    shaderInfo.pCode = reinterpret_cast<const quint32*>( m_frag.data() );

    vk::ShaderModule fragShaderModule;

    try {
        fragShaderModule = m_dev.createShaderModule( shaderInfo );
    } catch ( vk::SystemError err ) {
        qFatal( "Failed to create fragment shader module: %s", err.what() );
        return false;
    }

    std::array<vk::PipelineShaderStageCreateInfo, 2> stageInfo {
        vk::PipelineShaderStageCreateInfo { vk::PipelineShaderStageCreateFlags {}, vk::ShaderStageFlagBits::eVertex, vertShaderModule, "main" },
        vk::PipelineShaderStageCreateInfo { vk::PipelineShaderStageCreateFlags {}, vk::ShaderStageFlagBits::eFragment, fragShaderModule, "main" } };

    pipelineInfo.stageCount = stageInfo.size();
    pipelineInfo.pStages = stageInfo.data();

    vk::VertexInputBindingDescription vertexBinding( 0, 2 * sizeof( float ), vk::VertexInputRate::eVertex );

    vk::VertexInputAttributeDescription vertexAttr( 0, 0, vk::Format::eR32G32Sfloat, 0 );

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo( vk::PipelineVertexInputStateCreateFlags {}, 1, &vertexBinding, 1, &vertexAttr );

    pipelineInfo.pVertexInputState = &vertexInputInfo;

    std::vector<vk::DynamicState> dynStates { vk::DynamicState::eViewport, vk::DynamicState::eScissor };

    vk::PipelineDynamicStateCreateInfo dynamicInfo( vk::PipelineDynamicStateCreateFlags {}, 2, dynStates.data() );

    pipelineInfo.pDynamicState = &dynamicInfo;

    vk::Viewport viewport { 0, 0, static_cast<float>( m_size.width() ), static_cast<float>( m_size.height() ), 0.0f, 1.0f };
    vk::Rect2D scissor = { { 0, 0 }, { static_cast<uint32_t>( m_size.width() ), static_cast<uint32_t>( m_size.height() ) } };
    vk::PipelineViewportStateCreateInfo viewportInfo( vk::PipelineViewportStateCreateFlags {}, 1, &viewport, 1, &scissor );

    pipelineInfo.pViewportState = &viewportInfo;

    vk::PipelineInputAssemblyStateCreateInfo iaInfo( vk::PipelineInputAssemblyStateCreateFlags {}, vk::PrimitiveTopology::eTriangleStrip );

    pipelineInfo.pInputAssemblyState = &iaInfo;

    vk::PipelineRasterizationStateCreateInfo rsInfo;
    rsInfo.lineWidth = 1.0f;

    pipelineInfo.pRasterizationState = &rsInfo;

    vk::PipelineMultisampleStateCreateInfo msInfo( vk::PipelineMultisampleStateCreateFlags {}, vk::SampleCountFlagBits::e1 );

    pipelineInfo.pMultisampleState = &msInfo;

    vk::PipelineDepthStencilStateCreateInfo dsInfo;

    pipelineInfo.pDepthStencilState = &dsInfo;

    // SrcAlpha, One
    vk::PipelineColorBlendStateCreateInfo blendInfo;

    vk::PipelineColorBlendAttachmentState blend( true, vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                                                 vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                                                 vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB
                                                     | vk::ColorComponentFlagBits::eA );

    blendInfo.attachmentCount = 1;
    blendInfo.pAttachments = &blend;

    pipelineInfo.pColorBlendState = &blendInfo;

    pipelineInfo.layout = m_pipelineLayout;

    pipelineInfo.renderPass = m_renderPass;

    try {
        auto result = m_dev.createGraphicsPipeline( m_pipelineCache, pipelineInfo );

    } catch ( vk::SystemError err ) {
        qFatal( "Failed to create graphics pipeline: %s", err.what() );
        return false;
    }

    // Now just need some descriptors.
    std::vector<vk::DescriptorPoolSize> descPoolSizes { vk::DescriptorPoolSize { vk::DescriptorType::eUniformBufferDynamic, 1 } };

    vk::DescriptorPoolCreateInfo descPoolInfo( vk::DescriptorPoolCreateFlags {}, 1, sizeof( descPoolSizes ) / sizeof( descPoolSizes[0] ),
                                               descPoolSizes.data() );

    try {
        m_descriptorPool = m_dev.createDescriptorPool( descPoolInfo );
    } catch ( vk::SystemError err ) {
        qFatal( "Failed to create descriptor pool: %s", err.what() );
        return false;
    }

    vk::DescriptorSetAllocateInfo descAllocInfo( m_descriptorPool, 1, &m_resLayout );

    try {
        m_ubufDescriptor = m_dev.allocateDescriptorSets( descAllocInfo );
    } catch ( vk::SystemError err ) {
        qFatal( "Failed to allocate descriptor set: %s", err.what() );
        return false;
    }


    vk::WriteDescriptorSet writeInfo( m_ubufDescriptor[0], 0, {}, 1, vk::DescriptorType::eUniformBufferDynamic );


    vk::DescriptorBufferInfo bufInfo( m_ubuf, 0, UBUF_SIZE );


    writeInfo.pBufferInfo = &bufInfo;

    m_dev.updateDescriptorSets( 1, &writeInfo, 0, nullptr );
    return true;
}

void CustomTextureNode::sync() {

    if ( !m_initialized ) {
        prepareShader( VertexStage );
        prepareShader( FragmentStage );
        initialize();
        m_initialized = true;
    }

    bool needsNew { false };

    if ( !texture() ) {
        needsNew = true;
    }

    m_device_pixel_ratio = m_window->effectiveDevicePixelRatio();
    const QSize newSize = m_window->size() * m_device_pixel_ratio;

    if ( newSize != m_size ) {
        needsNew = true;
        m_size = newSize;
    }

    if ( needsNew ) {
        delete texture();
        freeTexture();
        buildTexture( m_size );
        QSGTexture* wrapper = QNativeInterface::QSGVulkanTexture::fromNative( m_texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_window, m_size );
        setTexture( wrapper );
        //        Q_ASSERT( wrapper->nativeInterface<QNativeInterface::QSGVulkanTexture>()->nativeImage() == m_texture );
    }

    //    m_t = float( static_cast<CustomTextureItem*>( m_item )->t() );

    m_t = ( ( ( int )( m_t * 100 ) % 100 ) + 1 ) / 100.0;
}

void CustomTextureNode::render() {
    if ( !m_initialized ) {
        return;
    }

    const uint currentFrameSlot = m_window->graphicsStateInfo().currentFrameSlot;

    vk::DeviceSize ubufOffset( currentFrameSlot * m_allocPerUbuf );

    try {
        auto memAloc = m_dev.mapMemory( m_ubufMem, ubufOffset, m_allocPerUbuf, vk::MemoryMapFlags {} );

    } catch ( vk::SystemError err ) { qFatal( "Failed to map uniform buffer memory: %s", err.what() ); }

    m_dev.unmapMemory( m_ubufMem );

    const std::array<float, 4> backgroundColor { 0.0f, 0.0f, 0.0f, 1.0f };
    vk::ClearValue clearColor( backgroundColor );

    vk::RenderPassBeginInfo rpBeginInfo( m_renderPass, m_textureFramebuffer, vk::Rect2D { m_size.width(), m_size.height() }, clearColor, nullptr );

    QSGRendererInterface* rif = m_window->rendererInterface();
    vk::CommandBuffer cmdBuf = *reinterpret_cast<vk::CommandBuffer*>( rif->getResource( m_window, QSGRendererInterface::CommandListResource ) );

    cmdBuf.beginRenderPass( rpBeginInfo, vk::SubpassContents::eInline );

    cmdBuf.bindPipeline( vk::PipelineBindPoint::eGraphics, m_pipeline );

    vk::DeviceSize vbufOffset { 0 };
    cmdBuf.bindVertexBuffers( 0, m_vbuf, vbufOffset );

    uint32_t dynamicOffset = m_allocPerUbuf * currentFrameSlot;

    cmdBuf.bindDescriptorSets( vk::PipelineBindPoint::eGraphics, m_pipelineLayout, 0, m_ubufDescriptor, dynamicOffset );

    vk::Viewport viewport { 0, 0, static_cast<float>( m_size.width() ), static_cast<float>( m_size.height() ), 0.0f, 1.0f };
    cmdBuf.setViewport( 0, viewport );

    vk::Rect2D scissor = { { 0, 0 }, { static_cast<uint32_t>( m_size.width() ), static_cast<uint32_t>( m_size.height() ) } };
    cmdBuf.setScissor( 0, scissor );

    cmdBuf.draw( 4, 1, 0, 0 );
    cmdBuf.endRenderPass();

    // Memory barrier before the texture can be used as a source.
    // Since we are not using a sub-pass, we have to do this explicitly.
    vk::ImageMemoryBarrier imageTransitionBarrier( vk::AccessFlags {}, vk::AccessFlags {}, vk::ImageLayout::eColorAttachmentOptimal,
                                                   vk::ImageLayout::eReadOnlyOptimal, 0, 0, m_texture, vk::ImageSubresourceRange {} );

    cmdBuf.pipelineBarrier( vk::PipelineStageFlags { vk::PipelineStageFlagBits::eColorAttachmentOutput },
                            vk::PipelineStageFlags { vk::PipelineStageFlagBits::eFragmentShader }, vk::DependencyFlags {}, 0, nullptr, 0, nullptr, 1,
                            &imageTransitionBarrier );
}

void CustomTextureNode::prepareShader( Stage stage ) {

    QString filename;

    if ( stage == VertexStage ) {
        filename = QLatin1String( ":/squircle.vert.spv" );
    } else {
        Q_ASSERT( stage == FragmentStage );
        filename = QLatin1String( ":/squircle.frag.spv" );
    }

    QFile f( filename );

    if ( !f.open( QIODevice::ReadOnly ) )
        qFatal( "Failed to read shader %s", qPrintable( filename ) );

    const QByteArray contents = f.readAll();

    if ( stage == VertexStage ) {
        m_vert.assign( contents.cbegin(), contents.cend() );
        Q_ASSERT( !m_vert.empty() );
    } else {
        m_frag.assign( contents.cbegin(), contents.cend() );
        Q_ASSERT( !m_frag.empty() );
    }
}
