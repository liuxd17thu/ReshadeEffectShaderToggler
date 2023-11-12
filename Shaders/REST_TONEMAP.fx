#include "ReShade.fxh"

void TonemapHDRtoSDR(in float4 pos : SV_Position, in float2 texcoord : Texcoord, out float4 o : SV_Target0)
{
    float4 color = tex2D(ReShade::BackBuffer, texcoord);

    o.rgb = color.rgb = color.rgb / (color.rgb + 1.0);
    o.a = color.a;
    o.rgb = saturate(o.rgb);
}

void TonemapSDRtoHDR(in float4 pos : SV_Position, in float2 texcoord : Texcoord, out float4 o : SV_Target0)
{
    float4 color = tex2D(ReShade::BackBuffer, texcoord);

    color.rgb = clamp(color.rgb, 0.0, 0.9999999); //avoid division by zero
    o.rgb = (-1.0 * color.rgb) / (color.rgb - 1.0);
    o.a = color.a;
}

technique REST_TONEMAP_TO_SDR
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader = TonemapHDRtoSDR;
    }
}

technique REST_TONEMAP_TO_HDR
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader = TonemapSDRtoHDR;
    }
}
