#include "ReShade.fxh"

void TonemapHDRtoSDR(in float4 pos : SV_Position, in float2 texcoord : Texcoord, out float4 o : SV_Target0)
{
    float4 color = tex2D(ReShade::BackBuffer, texcoord);
    o.rgb = color.rgb = color.rgb / (color.rgb + 1.0);
    o.a = color.a;
}

void TonemapSDRtoHDR(in float4 pos : SV_Position, in float2 texcoord : Texcoord, out float4 o : SV_Target0)
{
    float4 color = tex2D(ReShade::BackBuffer, texcoord);
    o.rgb = color.rgb = (1.0 * color.rgb) / (1.0 - color.rgb);
    o.a = color.a;
}

technique REST_TONEMAP_TO_SDR
{
    pass //update curr data RGB + depth
	{
		VertexShader = PostProcessVS;
		PixelShader  = TonemapHDRtoSDR; 
	}
}

technique REST_TONEMAP_TO_HDR
{
    pass //update curr data RGB + depth
	{
		VertexShader = PostProcessVS;
		PixelShader  = TonemapSDRtoHDR; 
	}
}

