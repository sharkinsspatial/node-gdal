#include "gdal_warper.hpp"
#include "utils/warp_options.hpp"
#include "gdal_common.hpp"
#include "gdal_spatial_reference.hpp"
#include "gdal_dataset.hpp"

namespace node_gdal {


void Warper::Initialize(Handle<Object> target)
{
	NODE_SET_METHOD(target, "reprojectImage", reprojectImage);
	NODE_SET_METHOD(target, "suggestedWarpOutput", suggestedWarpOutput);
}

/**
 * GDALReprojectImage() method with a ChunkAndWarpImage replaced with ChunkAndWarpMulti
 */
CPLErr GDALReprojectImageMulti( GDALDatasetH hSrcDS, const char *pszSrcWKT,
                    GDALDatasetH hDstDS, const char *pszDstWKT,
                    GDALResampleAlg eResampleAlg,
                    double dfWarpMemoryLimit,
                    double dfMaxError,
                    GDALProgressFunc pfnProgress, void *pProgressArg,
                    GDALWarpOptions *psOptions )

{
    GDALWarpOptions *psWOptions;

/* -------------------------------------------------------------------- */
/*      Setup a reprojection based transformer.                         */
/* -------------------------------------------------------------------- */
    void *hTransformArg;

    hTransformArg = 
        GDALCreateGenImgProjTransformer( hSrcDS, pszSrcWKT, hDstDS, pszDstWKT, 
                                         TRUE, 1000.0, 0 );

    if( hTransformArg == NULL )
        return CE_Failure;

/* -------------------------------------------------------------------- */
/*      Create a copy of the user provided options, or a defaulted      */
/*      options structure.                                              */
/* -------------------------------------------------------------------- */
    if( psOptions == NULL )
        psWOptions = GDALCreateWarpOptions();
    else
        psWOptions = GDALCloneWarpOptions( psOptions );

    psWOptions->eResampleAlg = eResampleAlg;

/* -------------------------------------------------------------------- */
/*      Set transform.                                                  */
/* -------------------------------------------------------------------- */
    if( dfMaxError > 0.0 )
    {
        psWOptions->pTransformerArg = 
            GDALCreateApproxTransformer( GDALGenImgProjTransform, 
                                         hTransformArg, dfMaxError );

        psWOptions->pfnTransformer = GDALApproxTransform;
    }
    else
    {
        psWOptions->pfnTransformer = GDALGenImgProjTransform;
        psWOptions->pTransformerArg = hTransformArg;
    }

/* -------------------------------------------------------------------- */
/*      Set file and band mapping.                                      */
/* -------------------------------------------------------------------- */
    int  iBand;

    psWOptions->hSrcDS = hSrcDS;
    psWOptions->hDstDS = hDstDS;

    if( psWOptions->nBandCount == 0 )
    {
        psWOptions->nBandCount = MIN(GDALGetRasterCount(hSrcDS),
                                     GDALGetRasterCount(hDstDS));
        
        psWOptions->panSrcBands = (int *) 
            CPLMalloc(sizeof(int) * psWOptions->nBandCount);
        psWOptions->panDstBands = (int *) 
            CPLMalloc(sizeof(int) * psWOptions->nBandCount);

        for( iBand = 0; iBand < psWOptions->nBandCount; iBand++ )
        {
            psWOptions->panSrcBands[iBand] = iBand+1;
            psWOptions->panDstBands[iBand] = iBand+1;
        }
    }

/* -------------------------------------------------------------------- */
/*      Set source nodata values if the source dataset seems to have    */
/*      any. Same for target nodata values                              */
/* -------------------------------------------------------------------- */
    for( iBand = 0; iBand < psWOptions->nBandCount; iBand++ )
    {
        GDALRasterBandH hBand = GDALGetRasterBand( hSrcDS, iBand+1 );
        int             bGotNoData = FALSE;
        double          dfNoDataValue;

        if (GDALGetRasterColorInterpretation(hBand) == GCI_AlphaBand)
        {
            psWOptions->nSrcAlphaBand = iBand + 1;
        }

        dfNoDataValue = GDALGetRasterNoDataValue( hBand, &bGotNoData );
        if( bGotNoData )
        {
            if( psWOptions->padfSrcNoDataReal == NULL )
            {
                int  ii;

                psWOptions->padfSrcNoDataReal = (double *) 
                    CPLMalloc(sizeof(double) * psWOptions->nBandCount);
                psWOptions->padfSrcNoDataImag = (double *) 
                    CPLMalloc(sizeof(double) * psWOptions->nBandCount);

                for( ii = 0; ii < psWOptions->nBandCount; ii++ )
                {
                    psWOptions->padfSrcNoDataReal[ii] = -1.1e20;
                    psWOptions->padfSrcNoDataImag[ii] = 0.0;
                }
            }

            psWOptions->padfSrcNoDataReal[iBand] = dfNoDataValue;
        }

        // Deal with target band
        hBand = GDALGetRasterBand( hDstDS, iBand+1 );
        if (hBand && GDALGetRasterColorInterpretation(hBand) == GCI_AlphaBand)
        {
            psWOptions->nDstAlphaBand = iBand + 1;
        }

        dfNoDataValue = GDALGetRasterNoDataValue( hBand, &bGotNoData );
        if( bGotNoData )
        {
            if( psWOptions->padfDstNoDataReal == NULL )
            {
                int  ii;

                psWOptions->padfDstNoDataReal = (double *) 
                    CPLMalloc(sizeof(double) * psWOptions->nBandCount);
                psWOptions->padfDstNoDataImag = (double *) 
                    CPLMalloc(sizeof(double) * psWOptions->nBandCount);

                for( ii = 0; ii < psWOptions->nBandCount; ii++ )
                {
                    psWOptions->padfDstNoDataReal[ii] = -1.1e20;
                    psWOptions->padfDstNoDataImag[ii] = 0.0;
                }
            }

            psWOptions->padfDstNoDataReal[iBand] = dfNoDataValue;
        }
    }

/* -------------------------------------------------------------------- */
/*      Set the progress function.                                      */
/* -------------------------------------------------------------------- */
    if( pfnProgress != NULL )
    {
        psWOptions->pfnProgress = pfnProgress;
        psWOptions->pProgressArg = pProgressArg;
    }

/* -------------------------------------------------------------------- */
/*      Create a warp options based on the options.                     */
/* -------------------------------------------------------------------- */
    GDALWarpOperation  oWarper;
    CPLErr eErr;

    eErr = oWarper.Initialize( psWOptions );

    if( eErr == CE_None )
        eErr = oWarper.ChunkAndWarpMulti( 0, 0, 
                                          GDALGetRasterXSize(hDstDS),
                                          GDALGetRasterYSize(hDstDS) );

/* -------------------------------------------------------------------- */
/*      Cleanup.                                                        */
/* -------------------------------------------------------------------- */
    GDALDestroyGenImgProjTransformer( hTransformArg );

    if( dfMaxError > 0.0 )
        GDALDestroyApproxTransformer( psWOptions->pTransformerArg );
        
    GDALDestroyWarpOptions( psWOptions );

    return eErr;
}

/**
 * Reprojects a dataset.
 *
 * @throws Error
 * @method reprojectImage
 * @static
 * @for gdal
 * @param {object} options
 * @param {gdal.Dataset} options.src
 * @param {gdal.Dataset} options.dst
 * @param {gdal.SpatialReference} options.s_srs
 * @param {gdal.SpatialReference} options.t_srs
 * @param {String} [options.resampling] Resampling algorithm ({{#crossLink "Constants (GRA)"}}available options{{/crossLink}})
 * @param {gdal.Geometry} [options.cutline] Must be in src dataset pixel coordinates. Use CoordinateTransformation to convert between georeferenced coordinates and pixel coordinates
 * @param {Integer[]} [options.srcBands]
 * @param {Integer[]} [options.dstBands]
 * @param {Integer} [options.srcAlphaBand]
 * @param {Integer} [options.dstAlphaBand]
 * @param {Number} [options.srcNodata]
 * @param {Number} [options.dstNodata]
 * @param {Integer} [options.memoryLimit]
 * @param {Number} [options.maxError]
 * @param {Number} [options.multi]
 * @param {string[]|object} [options.options] Warp options (see: [reference](http://www.gdal.org/structGDALWarpOptions.html#a0ed77f9917bb96c7a9aabd73d4d06e08))
 */
NAN_METHOD(Warper::reprojectImage)
{
	NanScope();

	Handle<Object> obj;
	Handle<Value> prop;

	WarpOptions options;
	GDALWarpOptions* opts;
	std::string s_srs_str;
	std::string t_srs_str;
	SpatialReference* s_srs;
	SpatialReference* t_srs;
	double maxError = 0;

	NODE_ARG_OBJECT(0, "Warp options", obj);

	if(options.parse(obj)){
		NanReturnUndefined(); // error parsing options object
	} else {
		opts = options.get();
	}
	if(!opts->hDstDS){
		NanThrowTypeError("dst Dataset must be provided");
		NanReturnUndefined();
	}

	NODE_WRAPPED_FROM_OBJ(obj, "s_srs", SpatialReference, s_srs);
	NODE_WRAPPED_FROM_OBJ(obj, "t_srs", SpatialReference, t_srs);
	NODE_DOUBLE_FROM_OBJ_OPT(obj, "maxError", maxError);

	char *s_srs_wkt, *t_srs_wkt;
	if(s_srs->get()->exportToWkt(&s_srs_wkt)){
		NanThrowError("Error converting s_srs to WKT");
		NanReturnUndefined();
	}
	if(t_srs->get()->exportToWkt(&t_srs_wkt)){
		CPLFree(s_srs_wkt);
		NanThrowError("Error converting t_srs to WKT");
		NanReturnUndefined();
	}

	CPLErr err;

	if(options.useMultithreading()){
		err = GDALReprojectImageMulti(opts->hSrcDS, s_srs_wkt, opts->hDstDS, t_srs_wkt, opts->eResampleAlg, opts->dfWarpMemoryLimit, maxError, NULL, NULL, opts);
	} else {
		err = GDALReprojectImage(opts->hSrcDS, s_srs_wkt, opts->hDstDS, t_srs_wkt, opts->eResampleAlg, opts->dfWarpMemoryLimit, maxError, NULL, NULL, opts);
	}
	
	CPLFree(s_srs_wkt);
	CPLFree(t_srs_wkt);

	if(err) {
		NODE_THROW_CPLERR(err);
		NanReturnUndefined();
	}

	NanReturnUndefined();
}

/**
 * Used to determine the bounds and resolution of the output virtual file which
 * should be large enough to include all the input image.
 *
 * @throws Error
 * @method suggestedWarpOutput
 * @static
 * @for gdal
 * @param {object} options Warp options
 * @param {gdal.Dataset} options.src
 * @param {gdal.SpatialReference} options.s_srs
 * @param {gdal.SpatialReference} options.t_srs
 * @param {Number} [options.maxError=0]
 * @return {Object} An object containing `"rasterSize"` and `"geoTransform"` properties.
 */
NAN_METHOD(Warper::suggestedWarpOutput)
{
	NanScope();

	Handle<Object> obj;
	Handle<Value> prop;

	Dataset* ds;
	SpatialReference* s_srs;
	SpatialReference* t_srs;
	double maxError = 0;
	double geotransform[6];
	int w = 0, h = 0;

	NODE_ARG_OBJECT(0, "Warp options", obj);

	if(obj->HasOwnProperty(NanNew("src"))){
		prop = obj->Get(NanNew("src"));
		if(prop->IsObject() && !prop->IsNull() && NanHasInstance(Dataset::constructor, prop)){
			ds = ObjectWrap::Unwrap<Dataset>(prop.As<Object>());
			if(!ds->getDataset()){
				#if GDAL_VERSION_MAJOR < 2
				if(ds->getDatasource()) {
					NanThrowError("src dataset must be a raster dataset");
					NanReturnUndefined();
				}
				#endif
				NanThrowError("src dataset already closed");
				NanReturnUndefined();
			}
		} else {
			NanThrowTypeError("src property must be a Dataset object");
			NanReturnUndefined();
		}
	} else {
		NanThrowError("src dataset must be provided");
		NanReturnUndefined();
	}

	NODE_WRAPPED_FROM_OBJ(obj, "s_srs", SpatialReference, s_srs);
	NODE_WRAPPED_FROM_OBJ(obj, "t_srs", SpatialReference, t_srs);
	NODE_DOUBLE_FROM_OBJ_OPT(obj, "maxError", maxError);

	char *s_srs_wkt, *t_srs_wkt;
	if(s_srs->get()->exportToWkt(&s_srs_wkt)){
		NanThrowError("Error converting s_srs to WKT");
		NanReturnUndefined();
	}
	if(t_srs->get()->exportToWkt(&t_srs_wkt)){
		CPLFree(s_srs_wkt);
		NanThrowError("Error converting t_srs to WKT");
		NanReturnUndefined();
	}



	void *hTransformArg;
	void *hGenTransformArg = GDALCreateGenImgProjTransformer(ds->getDataset(), s_srs_wkt, NULL, t_srs_wkt, TRUE, 1000.0, 0 );
	GDALTransformerFunc pfnTransformer;

	if(maxError > 0.0){
		hTransformArg = GDALCreateApproxTransformer( GDALGenImgProjTransform, hGenTransformArg, maxError );
		pfnTransformer = GDALApproxTransform;
	} else {
		hTransformArg = hGenTransformArg;
		pfnTransformer = GDALGenImgProjTransform;
	}

	CPLErr err = GDALSuggestedWarpOutput(ds->getDataset(), pfnTransformer, hTransformArg, geotransform, &w, &h);


	CPLFree(s_srs_wkt);
	CPLFree(t_srs_wkt);
	GDALDestroyGenImgProjTransformer(hGenTransformArg);
	if(maxError > 0.0){
		GDALDestroyApproxTransformer(hTransformArg);
	}

	if(err) {
		NODE_THROW_CPLERR(err);
		NanReturnUndefined();
	}

	Handle<Array> result_geotransform = NanNew<Array>();
	result_geotransform->Set(0, NanNew<Number>(geotransform[0]));
	result_geotransform->Set(1, NanNew<Number>(geotransform[1]));
	result_geotransform->Set(2, NanNew<Number>(geotransform[2]));
	result_geotransform->Set(3, NanNew<Number>(geotransform[3]));
	result_geotransform->Set(4, NanNew<Number>(geotransform[4]));
	result_geotransform->Set(5, NanNew<Number>(geotransform[5]));

	Handle<Object> result_size = NanNew<Object>();
	result_size->Set(NanNew("x"), NanNew<Integer>(w));
	result_size->Set(NanNew("y"), NanNew<Integer>(h));

	Handle<Object> result = NanNew<Object>();
	result->Set(NanNew("rasterSize"), result_size);
	result->Set(NanNew("geoTransform"), result_geotransform);

	NanReturnValue(result);
}


} //node_gdal namespace