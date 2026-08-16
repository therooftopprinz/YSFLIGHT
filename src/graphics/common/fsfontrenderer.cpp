#include "graphics/common/fsfontrenderer.h"
#include <yssystemfont.h>
#include <fsgui.h>


YsSystemFontRenderer fsUnicodeRenderer;
YsFixedFontRenderer fsAsciiRenderer;
FsDirectFixedFontRenderer fsDirectFixedFontRenderer;



////////////////////////////////////////////////////////////



int FsDirectFixedFontRenderer::GetFontHeight(void) const
{
	return fontHei;
}

int FsDirectFixedFontRenderer::GetFontWidth(void) const
{
	return fontWid;
}

YSRESULT FsDirectFixedFontRenderer::RemakeFont(void)
{
	return RequestDefaultFontWithPixelHeight(fontHei);
}

YSRESULT FsDirectFixedFontRenderer::RenderAsciiString(int leftX,int bottomY,const char str[],const YsColor &col)
{
	int nWid,nHei;
	YsFontRenderer::CountStringDimension(nWid,nHei,str);

	if(1==nHei)
	{
		return RenderAsciiStringSingleLine(leftX,bottomY,str,col);
	}

	const int fontHei=GetFontHeight();

	int i=0,y=bottomY-(nHei-1)*fontHei;
	YsString buf;
	for(;;)
	{
		if(0==str[i] || '\n'==str[i])
		{
			RenderAsciiStringSingleLine(leftX,y,buf,col);
			y+=fontHei;
			buf.Set("");
		}
		else
		{
			buf.Append(str[i]);
		}

		if(0==str[i])
		{
			break;
		}
		i++;
	}

	return YSOK;
}

YSRESULT FsSetFont(const char /*fontName*/[],int fontHeight)
{
	fsUnicodeRenderer.RequestDefaultFontWithPixelHeight(fontHeight);
	fsAsciiRenderer.RequestDefaultFontWithPixelHeight(fontHeight);
	fsDirectFixedFontRenderer.RequestDefaultFontWithPixelHeight(fontHeight);

	// On Linux the system font renderer rasterizes through X11.  Without a
	// display it silently returns empty bitmaps, which leaves every dialog
	// label blank, so use the built-in bitmap font instead.
	int probeWid=0,probeHei=0;
	if(YSOK!=fsUnicodeRenderer.GetTightRenderSize(probeWid,probeHei,L"X") ||
	   0>=probeWid || 0>=probeHei)
	{
		if(FsGuiObject::defUnicodeRenderer==&fsUnicodeRenderer)
		{
			FsGuiObject::defUnicodeRenderer=&fsAsciiRenderer;
		}
	}
	return YSOK;
}

