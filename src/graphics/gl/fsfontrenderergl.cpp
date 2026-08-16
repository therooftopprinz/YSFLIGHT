#include "graphics/common/fsfontrenderer.h"
#include "graphics/common/fsopengl.h"

#include <ysglfontdata.h>
#include <ysglbmpblit.h>
#include <yssystemfont.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifndef __APPLE__
#include <GL/gl.h>
#include <GL/glu.h>
#else
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#endif



extern int ysScnFontBitmapBase;  // defined in ysscenerygl.cpp

YSRESULT FsDirectFixedFontRenderer::RequestDefaultFontWithPixelHeight(unsigned int fontHeight)
{
	ysScnFontBitmapBase=FS_GL_FONT_BITMAP_BASE;

	fontPtr=YsGlSelectFontBitmapPointerByHeight(&fontWid,&fontHei,fontHeight);
	if(NULL!=fontPtr)
	{
		YsGlMakeFontBitmapDisplayList(FS_GL_FONT_BITMAP_BASE,fontPtr,fontWid,fontHei);
		return YSOK;
	}
	return YSERR;
}

YSRESULT FsDirectFixedFontRenderer::RenderAsciiStringSingleLine(int leftX,int bottomY,const char str[],const YsColor &col)
{
	if(NULL==fontPtr)
	{
		return YSERR;
	}
	YsGlBlitFontString2D(leftX,bottomY,str,fontPtr,fontWid,fontHei,col.Rf(),col.Gf(),col.Bf());
	return YSOK;
}
