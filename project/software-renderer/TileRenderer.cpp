#include "TileRenderer.h"


namespace nme
{
	
	TileRenderer::TileRenderer(const GraphicsJob &inJob, const GraphicsPath &inPath)
	{
      mBlendMode = bmNormal;
		mFill = inJob.mFill->AsBitmapFill();
		mFill->IncRef();
		mFiller = Filler::Create(mFill);
		const UserPoint *point = (const UserPoint *)&inPath.data[inJob.mData0];
		int n = inJob.mCommandCount;
		for(int j=0; j<n; j++)
		{
			int c = (inPath.commands[j+inJob.mCommand0]);
			switch(c)
			{
            case pcBlendModeAdd:
               mBlendMode = bmAdd;
               break;
				case pcWideMoveTo:
				case pcWideLineTo:
				case pcCurveTo:
						point++;
				case pcMoveTo:
				case pcBeginAt:
				case pcLineTo:
						point++;
						break;
				case pcTile:
				case pcTileTrans:
				case pcTileCol:
				case pcTileTransCol:
						{
							TileData data(point,c);
							mTileData.push_back(data);
							point+=3;
							if (c & pcTile_Trans_Bit)
								point+=2;
							if (c & pcTile_Col_Bit)
								point+=2;
						}
			}
		}
	}
	
	
	TileRenderer::~TileRenderer()
	{
		mFill->DecRef();
		delete mFiller;
	}
	
	
	void TileRenderer::Destroy() {
		delete this;
	}
	
	
	bool TileRenderer::GetExtent(const Transform &inTransform,Extent2DF &ioExtent)
	{
		/*
		printf("In extent %f,%f ... %f,%f\n",
				 ioExtent.mMinX, ioExtent.mMinY,
				 ioExtent.mMaxX, ioExtent.mMaxY );
		*/

		for(int i=0;i<mTileData.size();i++)
		{
			TileData &data= mTileData[i];
			for(int c=0;c<4;c++)
			{
				UserPoint corner(data.mPos);
				if (c&1) corner.x += data.mRect.w;
				if (c&2) corner.y += data.mRect.h;
				ioExtent.Add( inTransform.mMatrix->Apply(corner.x,corner.y) );
			}
		}
		/*
		printf("Got extent %f,%f ... %f,%f\n",
				 ioExtent.mMinX, ioExtent.mMinY,
				 ioExtent.mMaxX, ioExtent.mMaxY );
		*/
		return true;
	}
	
	
	bool TileRenderer::Hits(const RenderState &inState)
	{
		return false;
	}
	
	
	bool TileRenderer::Render(const RenderTarget &inTarget, const RenderState &inState)
	{
		Surface *s = mFill->bitmapData;
		double bmp_scale_x = 1.0/s->Width();
		double bmp_scale_y = 1.0/s->Height();
		// Todo:skew
		bool is_stretch = (inState.mTransform.mMatrix->m00!=1.0 ||
								 inState.mTransform.mMatrix->m11!=1.0) &&
								 ( inState.mTransform.mMatrix->m00 > 0 &&
									inState.mTransform.mMatrix->m11 > 0  );

		for(int i=0;i<mTileData.size();i++)
		{
			TileData &data= mTileData[i];

			BlendMode blend = data.mHasColour ? ( mBlendMode==bmAdd ? bmTintedAdd : bmTinted ):
                                               mBlendMode;
			UserPoint corner(data.mPos);
			UserPoint pos = inState.mTransform.mMatrix->Apply(corner.x,corner.y);

			if ( (is_stretch || data.mHasTrans) )
			{
				// Can use stretch if there is no skew and no colour transform...
				if (!data.mHasColour && (!data.mHasTrans) &&  mBlendMode==bmNormal )
				{
					UserPoint p0 = pos;
					pos = inState.mTransform.mMatrix->Apply(corner.x+data.mRect.w,corner.y+data.mRect.h);
					s->StretchTo(inTarget, data.mRect, DRect(p0.x,p0.y,pos.x,pos.y,true));
				}
				else
				{
					int tile_alpha = 256;
               bool just_alpha = (data.mHasColour) &&
                                 ((data.mColour&0x00ffffff ) == 0x00ffffff);
					if (data.mHasColour && mBlendMode==bmNormal)
					{ 
						tile_alpha = data.mColour>>24;
						if (tile_alpha>0) tile_alpha++;
					}
					// Create alpha mask...
					UserPoint p[4];
					p[0] = inState.mTransform.mMatrix->Apply(corner.x,corner.y);
					if (data.mHasTrans)
					{
						p[1] = inState.mTransform.mMatrix->Apply(
                            corner.x + data.mRect.w*data.mTransX.x,
                            corner.y + data.mRect.w*data.mTransY.x);
						p[2] = inState.mTransform.mMatrix->Apply(
                            corner.x + data.mRect.w*data.mTransX.x + data.mRect.h*data.mTransX.y,
                            corner.y + data.mRect.w*data.mTransY.x + data.mRect.h*data.mTransY.y );
						p[3] = inState.mTransform.mMatrix->Apply(
                            corner.x + data.mRect.h*data.mTransX.y,
                            corner.y + data.mRect.h*data.mTransY.y );
					}
					else
					{
						p[1] = inState.mTransform.mMatrix->Apply(corner.x+data.mRect.w,corner.y);
						p[2] = inState.mTransform.mMatrix->Apply(corner.x+data.mRect.w,corner.y+data.mRect.h);
						p[3] = inState.mTransform.mMatrix->Apply(corner.x,corner.y+data.mRect.h);
					}
					
					Extent2DF extent;
					extent.Add(p[0]);
					extent.Add(p[1]);
					extent.Add(p[2]);
					extent.Add(p[3]);
					
					// Get bounding pixel rect
					Rect rect = inState.mTransform.GetTargetRect(extent);
					
					// Intersect with clip rect ...
					Rect visible_pixels = rect.Intersect(inState.mClipRect);
               if (!visible_pixels.HasPixels())
                  continue;
					
               Rect alpha_rect(visible_pixels);
               bool offscreen_buffer = mBlendMode!=bmNormal;
               if (offscreen_buffer)
               {
                  for(int i=0;i<4;i++)
                  {
                     p[i].x -= visible_pixels.x;
                     p[i].y -= visible_pixels.y;
                  }
                  alpha_rect.x -= visible_pixels.x;
                  alpha_rect.y -= visible_pixels.y;
               }

					int aa = 1;
					SpanRect *span = new SpanRect(alpha_rect,aa);
					for(int i=0;i<4;i++)
						span->Line<false,false>(
							  Fixed10( p[i].x + 0.5 , p[i].y + 0.5  ),
							  Fixed10( p[(i+1)&3].x + 0.5 , p[(i+1)&3].y + 0.5 ) );
					
					AlphaMask *alpha = span->CreateMask(inState.mTransform,tile_alpha);
					delete span;

					float uvt[6];
					uvt[0] = (data.mRect.x) * bmp_scale_x;
					uvt[1] = (data.mRect.y) * bmp_scale_y;
					uvt[2] = (data.mRect.x + data.mRect.w) * bmp_scale_x;
					uvt[3] = (data.mRect.y) * bmp_scale_y;
					uvt[4] = (data.mRect.x + data.mRect.w) * bmp_scale_x;
					uvt[5] = (data.mRect.y + data.mRect.h) * bmp_scale_y;
					mFiller->SetMapping(p,uvt,2);
					
               // Can render straight to surface ....
               if (!offscreen_buffer)
               {
                  if (data.mHasTrans && !just_alpha)
                  {
                     ColorTransform buf;
                     RenderState col_state(inState);
                     ColorTransform tint;
                     tint.redMultiplier =	((data.mColour)	& 0xff) * one_on_255;
                     tint.greenMultiplier = ((data.mColour>>8) & 0xff) * one_on_255;
                     tint.blueMultiplier =  ((data.mColour>>16)  & 0xff) * one_on_255;
                     col_state.CombineColourTransform(inState, &tint, &buf);
                     mFiller->Fill(*alpha,0,0,inTarget,col_state);
                  }
                  else
                     mFiller->Fill(*alpha,0,0,inTarget,inState);
               }
               else
               {
                  // Create temp surface
                  SimpleSurface *tmp = new SimpleSurface(visible_pixels.w,visible_pixels.h, pfARGB);
                  tmp->IncRef();
                  tmp->Zero();
                  {
                  AutoSurfaceRender tmp_render(tmp);
                  const RenderTarget &target = tmp_render.Target();
                  mFiller->Fill(*alpha,0,0,target,inState);
                  }

				      tmp->BlitTo(inTarget, Rect(0,0,visible_pixels.w,visible_pixels.h),
                          visible_pixels.x, visible_pixels.y,
                         just_alpha ? bmAdd : blend, 0, data.mColour | 0xff000000);

                  tmp->DecRef();
               }

					
					alpha->Dispose();
				}
			}
			else
				s->BlitTo(inTarget, data.mRect, (int)(pos.x), (int)(pos.y), blend, 0, data.mColour);
		}
		
		return true;
	}

}
