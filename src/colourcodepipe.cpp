#define WITH_D3D
#include <rw.h>
#include "rwgta.h"
#include <assert.h>

namespace gta {

bool renderColourCoded;
rw::RGBA colourCode;

#ifdef RW_D3D9

using namespace rw;
using namespace d3d;
using namespace d3d9;

void *colourcode_PS;

enum {
	PSLOC_globalColor = 1
};


void
colourCodeRenderCB(Atomic *atomic, d3d9::InstanceDataHeader *header)
{
	Geometry *geo = atomic->geometry;

	setStreamSource(0, header->vertexStream[0].vertexBuffer, 0, header->vertexStream[0].stride);
	setIndices(header->indexBuffer);
	setVertexDeclaration(header->vertexDeclaration);

	uint32 flags = geo->flags;
	geo->flags &= ~Geometry::LIGHT;
	lightingCB_Shader(atomic);
	geo->flags = flags;

	uploadMatrices(atomic->getFrame()->getLTM());

	setVertexShader(default_amb_VS);
	setPixelShader(colourcode_PS);

	RGBAf c;
	convColor(&c, &colourCode);
	d3ddevice->SetPixelShaderConstantF(PSLOC_globalColor, (float*)&c, 1);

	InstanceData *inst = header->inst;
	uint32 blend;
	for(uint32 i = 0; i < header->numMeshes; i++){
		d3d::setTexture(0, inst->material->texture);
		SetRenderState(VERTEXALPHA, inst->vertexAlpha || colourCode.alpha != 255);

		d3d::getRenderState(D3DRS_ALPHABLENDENABLE, &blend);
		if(renderColourCoded)
			d3d::setRenderState(D3DRS_ALPHABLENDENABLE, 0);

		d3d9::drawInst(header, inst);

		d3d::setRenderState(D3DRS_ALPHABLENDENABLE, blend);
		inst++;
	}
}

rw::ObjPipeline*
makeColourCodePipeline(void)
{
	{
#include "d3d_shaders/colourcode_PS.inc"
		colourcode_PS = createPixelShader(colourcode_PS_cso);
		assert(colourcode_PS);
	}

	d3d9::ObjPipeline *pipe = d3d9::ObjPipeline::create();
	pipe->instanceCB = defaultInstanceCB;
	pipe->uninstanceCB = defaultUninstanceCB;
	pipe->renderCB = colourCodeRenderCB;
	return pipe;
}

static IDirect3DSurface9 *colourCodeSavedTarget;
static IDirect3DSurface9 *colourCodeSavedDepth;
static IDirect3DSurface9 *colourCodeTarget;
static IDirect3DSurface9 *colourCodeDepth;
static D3DVIEWPORT9 colourCodeSavedViewport;
static bool colourCodePassActive;
static bool colourCodeTargetSwitched;

static void
releaseSurface(IDirect3DSurface9 *&surface)
{
	if(surface){
		surface->Release();
		surface = nil;
	}
}

static void
releaseColourCodePassSurfaces(void)
{
	releaseSurface(colourCodeDepth);
	releaseSurface(colourCodeTarget);
	releaseSurface(colourCodeSavedDepth);
	releaseSurface(colourCodeSavedTarget);
	colourCodePassActive = false;
	colourCodeTargetSwitched = false;
}

bool
BeginColourCodePass(rw::Camera *camera, const rw::RGBA *clearColour)
{
	if(colourCodePassActive || camera == nil || clearColour == nil)
		return false;

	IDirect3DSurface9 *savedTarget = nil;
	IDirect3DSurface9 *savedDepth = nil;
	IDirect3DSurface9 *pickTarget = nil;
	IDirect3DSurface9 *pickDepth = nil;
	D3DVIEWPORT9 savedViewport;
	D3DSURFACE_DESC targetDesc, depthDesc;

	if(FAILED(d3ddevice->GetRenderTarget(0, &savedTarget)) || savedTarget == nil)
		return false;
	if(FAILED(savedTarget->GetDesc(&targetDesc)) ||
	   FAILED(d3ddevice->GetDepthStencilSurface(&savedDepth)) || savedDepth == nil ||
	   FAILED(savedDepth->GetDesc(&depthDesc)) ||
	   FAILED(d3ddevice->GetViewport(&savedViewport))){
		releaseSurface(savedDepth);
		releaseSurface(savedTarget);
		return false;
	}

	bool needsNonMsaaTarget = targetDesc.MultiSampleType != D3DMULTISAMPLE_NONE;
	if(needsNonMsaaTarget){
		if(FAILED(d3ddevice->CreateRenderTarget(targetDesc.Width, targetDesc.Height,
				targetDesc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &pickTarget, nil)) ||
		   pickTarget == nil ||
		   FAILED(d3ddevice->CreateDepthStencilSurface(targetDesc.Width, targetDesc.Height,
				depthDesc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &pickDepth, nil)) ||
		   pickDepth == nil){
			releaseSurface(pickDepth);
			releaseSurface(pickTarget);
			releaseSurface(savedDepth);
			releaseSurface(savedTarget);
			return false;
		}

		// A multisampled depth surface cannot remain bound while switching to a
		// non-multisampled render target. Bypass librw's cache temporarily, then
		// restore the exact cached surfaces in EndColourCodePass.
		HRESULT bindResult = d3ddevice->SetDepthStencilSurface(nil);
		if(SUCCEEDED(bindResult))
			bindResult = d3ddevice->SetRenderTarget(0, pickTarget);
		if(SUCCEEDED(bindResult))
			bindResult = d3ddevice->SetDepthStencilSurface(pickDepth);
		if(SUCCEEDED(bindResult))
			bindResult = d3ddevice->SetViewport(&savedViewport);
		if(FAILED(bindResult)){
			d3ddevice->SetDepthStencilSurface(nil);
			d3ddevice->SetRenderTarget(0, savedTarget);
			d3ddevice->SetDepthStencilSurface(savedDepth);
			d3ddevice->SetViewport(&savedViewport);
			releaseSurface(pickDepth);
			releaseSurface(pickTarget);
			releaseSurface(savedDepth);
			releaseSurface(savedTarget);
			return false;
		}
	}

	D3DCOLOR clear = D3DCOLOR_RGBA(clearColour->red, clearColour->green,
		clearColour->blue, clearColour->alpha);
	if(FAILED(d3ddevice->Clear(0, nil, D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,
	                         clear, 1.0f, 0))){
		if(needsNonMsaaTarget){
			d3ddevice->SetDepthStencilSurface(nil);
			d3ddevice->SetRenderTarget(0, savedTarget);
			d3ddevice->SetDepthStencilSurface(savedDepth);
			d3ddevice->SetViewport(&savedViewport);
		}
		releaseSurface(pickDepth);
		releaseSurface(pickTarget);
		releaseSurface(savedDepth);
		releaseSurface(savedTarget);
		return false;
	}

	colourCodeSavedTarget = savedTarget;
	colourCodeSavedDepth = savedDepth;
	colourCodeTarget = needsNonMsaaTarget ? pickTarget : savedTarget;
	colourCodeDepth = pickDepth;
	colourCodeSavedViewport = savedViewport;
	colourCodeTargetSwitched = needsNonMsaaTarget;
	colourCodePassActive = true;
	return true;
}

void
EndColourCodePass(void)
{
	if(!colourCodePassActive)
		return;
	if(colourCodeTargetSwitched){
		d3ddevice->SetDepthStencilSurface(nil);
		d3ddevice->SetRenderTarget(0, colourCodeSavedTarget);
		d3ddevice->SetDepthStencilSurface(colourCodeSavedDepth);
		d3ddevice->SetViewport(&colourCodeSavedViewport);
	}

	// When no target switch was needed, colourCodeTarget aliases the saved
	// target and must only be released once.
	if(!colourCodeTargetSwitched)
		colourCodeTarget = nil;
	releaseColourCodePassSurfaces();
}

static IDirect3DSurface9*
copyColourCodeTarget(D3DSURFACE_DESC *desc)
{
	IDirect3DSurface9 *source = colourCodePassActive ? colourCodeTarget : nil;
	IDirect3DSurface9 *copy = nil;
	bool releaseSource = false;
	if(source == nil){
		if(FAILED(d3ddevice->GetRenderTarget(0, &source)) || source == nil)
			return nil;
		releaseSource = true;
	}

	HRESULT result = source->GetDesc(desc);
	if(SUCCEEDED(result) && desc->MultiSampleType == D3DMULTISAMPLE_NONE)
		result = d3ddevice->CreateOffscreenPlainSurface(desc->Width, desc->Height,
			desc->Format, D3DPOOL_SYSTEMMEM, &copy, nil);
	else if(SUCCEEDED(result))
		result = D3DERR_INVALIDCALL;
	if(SUCCEEDED(result))
		result = d3ddevice->GetRenderTargetData(source, copy);
	if(releaseSource)
		source->Release();
	if(FAILED(result)){
		releaseSurface(copy);
		return nil;
	}
	return copy;
}

static bool
isReadableColourCodeFormat(D3DFORMAT format)
{
	return format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8;
}

int32
GetColourCode(int x, int y)
{
	D3DSURFACE_DESC desc;
	IDirect3DSurface9 *surf = copyColourCodeTarget(&desc);
	if(surf == nil)
		return -1;
	if(!isReadableColourCodeFormat(desc.Format)){
		releaseSurface(surf);
		return -1;
	}
	if(x < 0 || y < 0 || x >= (int)desc.Width || y >= (int)desc.Height){
		surf->Release();
		return 0;
	}

	int32 res = -1;
	D3DLOCKED_RECT d3dlr;
	if(SUCCEEDED(surf->LockRect(&d3dlr, nil,
	                           D3DLOCK_NO_DIRTY_UPDATE|D3DLOCK_READONLY))){
		uint8 *col = (uint8*)d3dlr.pBits + d3dlr.Pitch*y + x*4;
		res = col[0]<<16 | col[1]<<8 | col[2];
		surf->UnlockRect();
	}
	surf->Release();
	return res;
}

int
GetColourCodesInRect(int rx, int ry, int w, int h, int32 *out, int maxOut)
{
	int count = 0;
	if(w <= 0 || h <= 0 || out == nil || maxOut <= 0) return 0;

	D3DSURFACE_DESC desc;
	IDirect3DSurface9 *surf = copyColourCodeTarget(&desc);
	if(surf == nil)
		return -1;
	if(!isReadableColourCodeFormat(desc.Format)){
		releaseSurface(surf);
		return -1;
	}

	long long rawXEnd = (long long)rx + w;
	long long rawYEnd = (long long)ry + h;
	int xStart = rx < 0 ? 0 : rx;
	int yStart = ry < 0 ? 0 : ry;
	int xEnd = rawXEnd > (long long)desc.Width ? (int)desc.Width : (int)rawXEnd;
	int yEnd = rawYEnd > (long long)desc.Height ? (int)desc.Height : (int)rawYEnd;
	if(xStart >= xEnd || yStart >= yEnd){
		surf->Release();
		return 0;
	}

	D3DLOCKED_RECT d3dlr;
	HRESULT lockResult = surf->LockRect(&d3dlr, nil,
		D3DLOCK_NO_DIRTY_UPDATE|D3DLOCK_READONLY);
	if(SUCCEEDED(lockResult)){
		for(int row = yStart; row < yEnd; row++){
			uint8 *scanline = (uint8*)d3dlr.pBits + d3dlr.Pitch*row;
			for(int col = xStart; col < xEnd; col++){
				uint8 *px = scanline + col*4;
				int32 code = px[0]<<16 | px[1]<<8 | px[2];
				if(code == 0) continue;
				bool found = false;
				for(int j = 0; j < count; j++)
					if(out[j] == code){ found = true; break; }
				if(!found){
					out[count++] = code;
					if(count >= maxOut) goto done;
				}
			}
		}
	done:
		surf->UnlockRect();
	}
	surf->Release();
	return SUCCEEDED(lockResult) ? count : -1;
}

#endif

#ifdef RW_GL3

using namespace rw;
using namespace gl3;

Shader *colourCodeShader;

void
colourCodeRenderCB(Atomic *atomic, gl3::InstanceDataHeader *header)
{
	Material *m;
	RGBAf col;
	Geometry *geo = atomic->geometry;

	setWorldMatrix(atomic->getFrame()->getLTM());
	uint32 flags = geo->flags;
	geo->flags &= ~Geometry::LIGHT;
	lightingCB(atomic);
	geo->flags = flags;

	setupVertexInput(header);

	InstanceData *inst = header->inst;
	int32 n = header->numMeshes;

	colourCodeShader->use();

	convColor(&col, &colourCode);
	setUniform(u_matColor, &col);

	while(n--){
		m = inst->material;

		setTexture(0, m->texture);
		rw::SetRenderState(VERTEXALPHA, inst->vertexAlpha || colourCode.alpha != 0xFF);

		int blend = getAlphaBlend();
		if(renderColourCoded)
			setAlphaBlend(false);

		drawInst(header, inst);

		setAlphaBlend(blend);
		inst++;
	}
	teardownVertexInput(header);
}

rw::ObjPipeline*
makeColourCodePipeline(void)
{
	{
#include "gl_shaders/colcode_vert.inc"
#include "gl_shaders/colcode_frag.inc"
	const char *vs[] = { shaderDecl, header_vert_src, colcode_vert_src, nil };
	const char *fs[] = { shaderDecl, header_frag_src, colcode_frag_src, nil };
	colourCodeShader = Shader::create(vs, fs);
	assert(colourCodeShader);
	}

	gl3::ObjPipeline *pipe = gl3::ObjPipeline::create();
	pipe->instanceCB = defaultInstanceCB;
	pipe->uninstanceCB = defaultUninstanceCB;
	pipe->renderCB = colourCodeRenderCB;
	return pipe;
}

bool
BeginColourCodePass(rw::Camera *camera, const rw::RGBA *clearColour)
{
	if(camera == nil || clearColour == nil)
		return false;
	// UI and preview rendering can leave another camera's viewport active.
	// Rebind this camera so the colour pass uses the same projection and GL
	// viewport as the visible scene before clearing and drawing it.
	camera->beginUpdate();
	camera->clear((rw::RGBA*)clearColour, rw::Camera::CLEARIMAGE|rw::Camera::CLEARZ);
	return true;
}

void
EndColourCodePass(void)
{
}

static rw::Raster*
getColourCodeRaster(void)
{
	// GLFW mouse events and the camera raster are in window coordinates; the
	// active GL viewport may be larger in framebuffer pixels on HiDPI displays.
	if(rw::engine == nil || rw::engine->currentCamera == nil)
		return nil;
	return rw::engine->currentCamera->frameBuffer;
}

static int
scaleLogicalCoord(int coord, int framebufferExtent, int logicalExtent)
{
	return (int)((long long)coord * framebufferExtent / logicalExtent);
}

static int
scaleLogicalEdge(int edge, int framebufferExtent, int logicalExtent)
{
	return (int)(((long long)edge * framebufferExtent + logicalExtent - 1) /
	             logicalExtent);
}

int32
GetColourCode(int x, int y)
{
	rw::RGBA col = { 0, 0, 0, 0 };
	int viewport[4];
	rw::Raster *raster = getColourCodeRaster();
	if(raster == nil || raster->width <= 0 || raster->height <= 0)
		return 0;
	if(x < 0 || y < 0 || x >= raster->width || y >= raster->height)
		return 0;
	glGetIntegerv(GL_VIEWPORT, viewport);
	if(viewport[2] <= 0 || viewport[3] <= 0)
		return 0;
	int pixelX = viewport[0] + scaleLogicalCoord(x, viewport[2], raster->width);
	int pixelY = scaleLogicalCoord(y, viewport[3], raster->height);
	if(pixelX < viewport[0] || pixelY < 0 ||
	   pixelX >= viewport[0] + viewport[2] || pixelY >= viewport[3])
		return 0;
	glReadPixels(pixelX, viewport[1] + viewport[3] - 1 - pixelY,
	             1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &col);
	return col.blue<<16 | col.green<<8 | col.red;
}

int
GetColourCodesInRect(int rx, int ry, int w, int h, int32 *out, int maxOut)
{
	int count = 0;
	if(w <= 0 || h <= 0 || out == nil || maxOut <= 0) return 0;

	int viewport[4];
	rw::Raster *raster = getColourCodeRaster();
	if(raster == nil || raster->width <= 0 || raster->height <= 0)
		return 0;
	glGetIntegerv(GL_VIEWPORT, viewport);
	if(viewport[2] <= 0 || viewport[3] <= 0)
		return 0;

	long long rawXEnd = (long long)rx + w;
	long long rawYEnd = (long long)ry + h;
	int xStart = rx < 0 ? 0 : rx;
	int yStart = ry < 0 ? 0 : ry;
	int xEnd = rawXEnd > raster->width ? raster->width : (int)rawXEnd;
	int yEnd = rawYEnd > raster->height ? raster->height : (int)rawYEnd;
	if(xStart >= xEnd || yStart >= yEnd)
		return 0;

	int pixelXStart = viewport[0] + scaleLogicalCoord(xStart, viewport[2], raster->width);
	int pixelYStart = scaleLogicalCoord(yStart, viewport[3], raster->height);
	int pixelXEnd = viewport[0] + scaleLogicalEdge(xEnd, viewport[2], raster->width);
	int pixelYEnd = scaleLogicalEdge(yEnd, viewport[3], raster->height);
	int viewportXEnd = viewport[0] + viewport[2];
	if(pixelXEnd > viewportXEnd) pixelXEnd = viewportXEnd;
	if(pixelYEnd > viewport[3]) pixelYEnd = viewport[3];
	int readWidth = pixelXEnd - pixelXStart;
	int readHeight = pixelYEnd - pixelYStart;
	if(readWidth <= 0 || readHeight <= 0)
		return 0;
	int glx = pixelXStart;
	int gly = viewport[1] + viewport[3] - pixelYEnd;
	rw::RGBA *pixels = (rw::RGBA*)rwMalloc(readWidth * readHeight * sizeof(rw::RGBA), 0);
	if(pixels == nil) return -1;
	glReadPixels(glx, gly, readWidth, readHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

	// glReadPixels returns bottom-up rows; iterate all pixels
	for(int i = 0; i < readWidth*readHeight; i++){
		int32 code = pixels[i].blue<<16 | pixels[i].green<<8 | pixels[i].red;
		if(code == 0) continue;
		bool found = false;
		for(int j = 0; j < count; j++)
			if(out[j] == code){ found = true; break; }
		if(!found){
			out[count++] = code;
			if(count >= maxOut) break;
		}
	}
	rwFree(pixels);
	return count;
}
#endif

}
