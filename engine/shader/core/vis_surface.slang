    // Экранные барицентрики треугольника из visbuffer. Дескрипторов нет:
    // вызывающий уже объявил gVerts, gIdx, gMeshlets, gInstances и включил camera.slang.

    struct VisTri {
        uint instanceId;
        uint triId;
        uint i0;
        uint i1;
        uint i2;
        GpuInstance inst;
        float3 w0;
        float3 w1;
        float3 w2;
        float3 faceN;
        float faceA;
        float3 n0;
        float3 n1;
        float3 n2;
        float bu;
        float bv;
        float bw;
        bool denomOk;
        bool inRange;
        float2 uv;
        float3 smoothN;
    };

VisTri loadVisTri(uint instanceId, uint tri, uint2 px) {
        VisTri t;
        t.instanceId = instanceId;
        t.triId = tri;
        t.inst = gInstances[instanceId];

        const MeshletRef meshlet = gMeshlets[t.inst.meshId];
        const uint at = meshlet.indexOffset + tri * 3u;

        t.i0 = meshlet.vertexOffset + gIdx[at];
        t.i1 = meshlet.vertexOffset + gIdx[at + 1u];
        t.i2 = meshlet.vertexOffset + gIdx[at + 2u];

        // 1. Позиции вершин в мировом пространстве
        const float4 h0 = float4(gVerts[t.i0].pos, 1.0);
        const float4 h1 = float4(gVerts[t.i1].pos, 1.0);
        const float4 h2 = float4(gVerts[t.i2].pos, 1.0);

        t.w0 = float3(dot(t.inst.world0, h0), dot(t.inst.world1, h0), dot(t.inst.world2, h0));
        t.w1 = float3(dot(t.inst.world0, h1), dot(t.inst.world1, h1), dot(t.inst.world2, h1));
        t.w2 = float3(dot(t.inst.world0, h2), dot(t.inst.world1, h2), dot(t.inst.world2, h2));

        // 2. Геометрическая нормаль треугольника
        const float3 rawN = cross(t.w1 - t.w0, t.w2 - t.w0);
        t.faceA = dot(rawN, rawN);
        t.faceN = t.faceA > 1e-12 ? normalize(rawN) : float3(0.0, 1.0, 0.0);

        // 3. Однородные (Homogeneous) 2D барицентрики строго через gFrame.viewProj БЕЗ деления на c.w!
        // Не зависят от invViewProj/cameraPos и не выворачиваются при c.w <= 0 за спиной камеры.
        const float4 c0 = mul(gFrame.viewProj, float4(t.w0, 1.0));
        const float4 c1 = mul(gFrame.viewProj, float4(t.w1, 1.0));
        const float4 c2 = mul(gFrame.viewProj, float4(t.w2, 1.0));

        const float2 ndc = (float2(px) + 0.5) * gFrame.invExtent * 2.0 - 1.0;
        const float2 q0 = c0.xy - ndc * c0.w;
        const float2 q1 = c1.xy - ndc * c1.w;
        const float2 q2 = c2.xy - ndc * c2.w;

        const float bw0 = q1.x * q2.y - q1.y * q2.x;
        const float bw1 = q2.x * q0.y - q2.y * q0.x;
        const float bw2 = q0.x * q1.y - q0.y * q1.x;
        const float bSum = bw0 + bw1 + bw2;

        t.denomOk = abs(bSum) > 1e-12;
        if (t.denomOk) {
            const float invSum = 1.0 / bSum;
            t.bu = bw0 * invSum;
            t.bv = bw1 * invSum;
            t.bw = bw2 * invSum;
        } else {
            t.bu = 1.0;
            t.bv = 0.0;
            t.bw = 0.0;
        }

        t.inRange = (t.bu >= -0.05 && t.bv >= -0.05 && t.bw >= -0.05);

        // 4. Интерполяция UV координат
        t.uv = float2(
            gVerts[t.i0].u * t.bu + gVerts[t.i1].u * t.bv + gVerts[t.i2].u * t.bw,
            gVerts[t.i0].v * t.bu + gVerts[t.i1].v * t.bv + gVerts[t.i2].v * t.bw
        );

        // 5. Вершинные нормали с защитой от склеенных 90-градусных углов на кубах и стенах
        float3 vn0 = float3(dot(t.inst.world0.xyz, gVerts[t.i0].nrm), dot(t.inst.world1.xyz, gVerts[t.i0].nrm), dot(t.inst.world2.xyz, gVerts[t.i0].nrm));
        float3 vn1 = float3(dot(t.inst.world0.xyz, gVerts[t.i1].nrm), dot(t.inst.world1.xyz, gVerts[t.i1].nrm), dot(t.inst.world2.xyz, gVerts[t.i1].nrm));
        float3 vn2 = float3(dot(t.inst.world0.xyz, gVerts[t.i2].nrm), dot(t.inst.world1.xyz, gVerts[t.i2].nrm), dot(t.inst.world2.xyz, gVerts[t.i2].nrm));

        vn0 = dot(vn0, vn0) > 1e-12 ? normalize(vn0) : t.faceN;
        vn1 = dot(vn1, vn1) > 1e-12 ? normalize(vn1) : t.faceN;
        vn2 = dot(vn2, vn2) > 1e-12 ? normalize(vn2) : t.faceN;

        // Если нормаль вершины загнута сильнее ~41 градуса от грани (склеенный угол куба) — используем плоскую нормаль грани
        if (abs(dot(vn0, t.faceN)) < 0.75 || abs(dot(vn1, t.faceN)) < 0.75 || abs(dot(vn2, t.faceN)) < 0.75) {
            vn0 = t.faceN;
            vn1 = t.faceN;
            vn2 = t.faceN;
        }

        t.n0 = vn0;
        t.n1 = vn1;
        t.n2 = vn2;

        t.smoothN = t.faceN;
        if (t.denomOk) {
            const float3 Ns = vn0 * t.bu + vn1 * t.bv + vn2 * t.bw;
            if (dot(Ns, Ns) > 1e-8) {
                t.smoothN = normalize(Ns);
            }
        }

        return t;
    }