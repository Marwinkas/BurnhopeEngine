#version 430 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
in vec3 vWorldPos[3];
in vec3 vNormal[3];
in vec2 vTexCoords[3];
out vec3 gWorldPos;
out vec3 gNormal;
out vec2 gTexCoords;
uniform vec3 gridMin;
uniform vec3 gridMax;
void main() {
        vec3 p1 = vWorldPos[1] - vWorldPos[0];
    vec3 p2 = vWorldPos[2] - vWorldPos[0];
    vec3 faceNormal = abs(cross(p1, p2));
        uint maxAxis = (faceNormal.x > faceNormal.y && faceNormal.x > faceNormal.z) ? 0 :
                   (faceNormal.y > faceNormal.z) ? 1 : 2;
    for(int i = 0; i < 3; i++) {
        gWorldPos = vWorldPos[i];
        gNormal = vNormal[i];
        gTexCoords = vTexCoords[i];
                vec3 uvw = (vWorldPos[i] - gridMin) / (gridMax - gridMin);
                vec3 ndc = uvw * 2.0 - 1.0;
                if (maxAxis == 0) {
            gl_Position = vec4(ndc.y, ndc.z, ndc.x, 1.0);
        } else if (maxAxis == 1) {
            gl_Position = vec4(ndc.x, ndc.z, ndc.y, 1.0);
        } else {
            gl_Position = vec4(ndc.x, ndc.y, ndc.z, 1.0);
        }
        EmitVertex();
    }
    EndPrimitive();
}