// Offline conversion and composition checks for the packed output route.
// Only this probe requests extended storage-image formats from nrvk's device.
#include <vulkan/vulkan.h>
static VkResult create_probe_device(VkPhysicalDevice physical, const VkDeviceCreateInfo* info,
                                   const VkAllocationCallbacks* allocator, VkDevice* device) {
    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physical, &supported);
    if (!supported.shaderStorageImageExtendedFormats) return VK_ERROR_FEATURE_NOT_PRESENT;
    VkPhysicalDeviceFeatures enabled = info->pEnabledFeatures ? *info->pEnabledFeatures : VkPhysicalDeviceFeatures{};
    enabled.shaderStorageImageExtendedFormats = VK_TRUE;
    auto create = *info;
    create.pEnabledFeatures = &enabled;
    return vkCreateDevice(physical, &create, allocator, device);
}
#define vkCreateDevice create_probe_device
#include "nrvk.hpp"
#undef vkCreateDevice
#include <array>
#include <filesystem>
#include <cmath>
#include <limits>
static uint32_t bits(float f) {uint32_t u;std::memcpy(&u,&f,4);return u;}
static float value(uint32_t u) {float f;std::memcpy(&f,&u,4);return f;}
static std::vector<float> boundaries(unsigned mantissa) {
    std::vector<float> result;
    auto unpack=[&](unsigned code) {
        const unsigned exponent=code>>mantissa,mant=code&((1u<<mantissa)-1);
        return exponent?std::ldexp(1.f+float(mant)/float(1u<<mantissa),int(exponent)-15)
                       :std::ldexp(float(mant),-14-int(mantissa));
    };
    for(unsigned code=0;code<(31u<<mantissa);++code) {
        const float lo=unpack(code),hi=unpack(code+1),mid=(lo+hi)*.5f;
        result.push_back(lo);
        for(int delta=-2;delta<=2;++delta)result.push_back(value(uint32_t(int64_t(bits(mid))+delta)));
        result.push_back(hi);
    }
    return result;
}
static void check_conversion(const char* shader) {
    nrvk::Context ctx;ctx.create();
    printf("GPU %s driver %s\n",ctx.gpu_name.c_str(),ctx.driver_name.c_str());
    const uint32_t w=513,h=257;const size_t count=size_t(w)*h;
    const auto r=boundaries(6),b=boundaries(5);
    std::vector<float> input(count*4);
    uint32_t random=0x9070u;
    auto next=[&] {random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(size_t i=0;i<count;++i) {
        input[4*i]=r[i%r.size()];input[4*i+1]=r[(i*43+17)%r.size()];input[4*i+2]=b[(i*71+5)%b.size()];input[4*i+3]=1;
        if(i>=r.size())for(unsigned c=0;c<3;++c)input[4*i+c]=value(next()&0xff7fffffu);
    }
    auto source=ctx.image(w,h,VK_FORMAT_R32G32B32A32_SFLOAT,false);
    auto a=ctx.image(w,h,VK_FORMAT_B10G11R11_UFLOAT_PACK32,false);
    auto c=ctx.image(w,h,VK_FORMAT_B10G11R11_UFLOAT_PACK32,false);
    ctx.upload(source,input.data(),input.size()*4);
    nrvk::Kernel kernel;kernel.create(ctx,shader,{},8,{&source,&c});
    ctx.one_shot([&](VkCommandBuffer cmd) {
        VkImageBlit blit{};blit.srcSubresource=blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        blit.srcOffsets[1]=blit.dstOffsets[1]={int32_t(w),int32_t(h),1};
        vkCmdBlitImage(cmd,source.handle,VK_IMAGE_LAYOUT_GENERAL,a.handle,VK_IMAGE_LAYOUT_GENERAL,1,&blit,VK_FILTER_NEAREST);
        const uint32_t push[]={w,h};
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,kernel.pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,kernel.layout,0,1,&kernel.set,0,nullptr);
        vkCmdPushConstants(cmd,kernel.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof push,push);
        vkCmdDispatch(cmd,(w+15)/16,(h+15)/16,1);
    });
    std::vector<uint32_t> av(count),cv(count);
    ctx.download(a,av.data(),av.size()*4);ctx.download(c,cv.data(),cv.size()*4);
    size_t different=0;
    for(size_t i=0;i<count;++i)if(av[i]!=cv[i]) {
        if(different<16)printf("DIFF i=%zu input=%08x,%08x,%08x blit=%08x store=%08x\n",i,bits(input[4*i]),bits(input[4*i+1]),bits(input[4*i+2]),av[i],cv[i]);
        ++different;
    }
    printf("CONVERSION_COMPARE pixels=%zu differences=%zu r11_boundaries=%zu r10_boundaries=%zu\n",count,different,r.size(),b.size());
    kernel.destroy();ctx.destroy(source);ctx.destroy(a);ctx.destroy(c);ctx.destroy();
    if (different) throw std::runtime_error("packed conversion changed bits");
}
static void dispatch(VkCommandBuffer cmd, nrvk::Kernel& k, uint32_t w, uint32_t h,
                     const void* push, uint32_t bytes) {
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,k.pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,k.layout,0,1,&k.set,0,nullptr);
    vkCmdPushConstants(cmd,k.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,bytes,push);
    vkCmdDispatch(cmd,(w+7)/8,(h+7)/8,1);
}
static void check_composition(const std::filesystem::path& dir) {
    nrvk::Context ctx;ctx.create();
    printf("GPU %s driver %s\n",ctx.gpu_name.c_str(),ctx.driver_name.c_str());
    const uint32_t w=257,h=129;const size_t count=size_t(w)*h;
    uint64_t compared=0;unsigned arrays=0;
    for(auto format:{VK_FORMAT_B10G11R11_UFLOAT_PACK32,VK_FORMAT_R16G16B16A16_SFLOAT}) {
        const unsigned bpp=format==VK_FORMAT_B10G11R11_UFLOAT_PACK32?4:8;
        std::vector<uint32_t> input(count*bpp/4);
        uint32_t random=0x13579bdu;
        auto next=[&] {random^=random<<13;random^=random>>17;random^=random<<5;return random;};
        if(bpp==4)for(size_t i=0;i<count;++i)
            input[i]=uint32_t(i%1984)|((next()%1984)<<11)|((next()%992)<<22);
        else {
            auto* halves=reinterpret_cast<uint16_t*>(input.data());
            for(size_t i=0;i<count*4;++i) {
                const uint32_t finite=uint32_t(i*251u)%63488u;
                halves[i]=uint16_t(finite<31744u?finite:(finite-31744u)|0x8000u);
            }
        }
        auto source=ctx.image(w,h,format,true,true,bpp==4);
        ctx.transition(source,VK_IMAGE_LAYOUT_GENERAL);
        ctx.upload(source,input.data(),input.size()*4);
        auto pa=ctx.image(w,h,VK_FORMAT_R32G32B32A32_SFLOAT,true);
        auto ka=ctx.image(w,h,VK_FORMAT_R32G32B32A32_SFLOAT,true);
        auto pb=ctx.image(w,h,VK_FORMAT_R32G32B32A32_SFLOAT,true);
        auto kb=ctx.image(w,h,VK_FORMAT_R32G32B32A32_SFLOAT,false);
        auto ma=ctx.image(w,h,VK_FORMAT_R32G32B32A32_SFLOAT,false);
        auto mb=ctx.image(w,h,VK_FORMAT_R32G32B32A32_SFLOAT,false);
        auto packed_a=ctx.image(w,h,VK_FORMAT_B10G11R11_UFLOAT_PACK32,false);
        auto packed_b=ctx.image(w,h,VK_FORMAT_B10G11R11_UFLOAT_PACK32,false);
        for(auto* im:{&pa,&ka,&pb})ctx.transition(*im,VK_IMAGE_LAYOUT_GENERAL);
        auto pas=pa,kas=ka,pbs=pb;pas.sampler=kas.sampler=pbs.sampler=VK_NULL_HANDLE;
        nrvk::Kernel encode_a,encode_b,transfer_a,transfer_b;
        encode_a.create(ctx,(dir/"runtime_encode_sampled.spv").string(),{},16,{&pas,&kas,&source});
        encode_b.create(ctx,(dir/"runtime_encode_direct.spv").string(),{},16,{&pbs,&kb,&source});
        transfer_a.create(ctx,(dir/"runtime_transfer.spv").string(),{},36,{&ma,&pa,&ka,&ma});
        // R11G11B10 tests the production alias: read the original and replace
        // exactly that texel. RGBA16F inputs check the arithmetic separately.
        auto storage_source=source; storage_source.sampler=VK_NULL_HANDLE;
        auto* destination=bpp==4 ? &storage_source : &packed_b;
        transfer_b.create(ctx,(dir/"runtime_transfer_packed.spv").string(),{},36,{&mb,&pb,&source,destination});
        std::vector<float> model(count*4);
        for(size_t i=0;i<model.size();++i)model[i]=(int(next()%2048)-256)/1024.f;
        std::vector<uint32_t> a(count),b(count);
        auto compare=[&](nrvk::Context::Image& ai,nrvk::Context::Image& bi,const char* what,float white) {
            ctx.download(ai,a.data(),a.size()*4);ctx.download(bi,b.data(),b.size()*4);
            size_t different=0,first=0;
            for(size_t i=0;i<a.size();++i)if(a[i]!=b[i]) {if(!different)first=i;++different;}
            printf("format=%u white=%.2f image=%s values=%zu differences=%zu first=%zu a=%08x b=%08x\n",unsigned(format),white,what,a.size(),different,first,a[first],b[first]);
            fflush(stdout);
            if(different)throw std::runtime_error("direct sampling changed FP32 bits");
            compared+=a.size();++arrays;
        };
        for(float white:{.01f,1.f,2.f,100.f}) {
            ctx.upload(source,input.data(),input.size()*4);
            struct Encode {uint32_t w,h;float white,knee;} encode{w,h,white,.75f};
            ctx.one_shot([&](VkCommandBuffer cmd) {
                dispatch(cmd,encode_a,w,h,&encode,sizeof encode);
                dispatch(cmd,encode_b,w,h,&encode,sizeof encode);
            });

            for(const auto& controls:std::array<std::array<float,3>,5>{{{1,1,4},{0,0,1},{2,1,8},{1,2,4},{.5f,4,2}}}) {
                ctx.upload(source,input.data(),input.size()*4);
                ctx.upload(ma,model.data(),model.size()*4);ctx.upload(mb,model.data(),model.size()*4);
                struct Transfer {uint32_t w,h,mw,mh,passthrough;float detail,colour,ratio,white;}
                    transfer{w,h,w,h,0,controls[0],controls[1],controls[2],white};
                ctx.one_shot([&](VkCommandBuffer cmd) {
                    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                    barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
                    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
                    dispatch(cmd,transfer_a,w,h,&transfer,sizeof transfer);
                    dispatch(cmd,transfer_b,w,h,&transfer,sizeof transfer);
                    barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
                    barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
                    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
                    VkImageBlit blit{};blit.srcSubresource=blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
                    blit.srcOffsets[1]=blit.dstOffsets[1]={int32_t(w),int32_t(h),1};
                    vkCmdBlitImage(cmd,ma.handle,VK_IMAGE_LAYOUT_GENERAL,packed_a.handle,VK_IMAGE_LAYOUT_GENERAL,1,&blit,VK_FILTER_NEAREST);
                });
                compare(packed_a,*destination,"packed-composition",white);
            }
        }
        encode_a.destroy();encode_b.destroy();transfer_a.destroy();transfer_b.destroy();
        for(auto* im:{&source,&pa,&ka,&pb,&kb,&ma,&mb,&packed_a,&packed_b})ctx.destroy(*im);
    }
    ctx.destroy();
    printf("PACKED_COMPOSITION_EXACT_OK arrays=%u values=%llu\n",arrays,(unsigned long long)compared);
}
int main(int argc,char** argv) try {
    if(argc!=3)throw std::runtime_error("runtime shader directory and conversion probe shader required");
    check_conversion(argv[2]);
    check_composition(argv[1]);
    return 0;
} catch(const std::exception& e) {fprintf(stderr,"PACKED_OUTPUT_FAILED %s\n",e.what());return 1;}
