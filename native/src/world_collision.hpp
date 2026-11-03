// Reused world-only collision traversal; no proprietary geometry is embedded.
namespace phys {
void MarkWorldBrushes(int index, std::vector<unsigned char> &seen, std::vector<unsigned char> &brushes)
{
    const auto& cm=*map;
    if ( index <= 0 || (unsigned)index >= cm.brushNodeCount || seen[index] ) return;
    seen[index] = 1;
    const native::BrushNode &n = cm.brushNodes[index];
    if ( !(n.contents & 0x2820011) ) return;
    if ( n.brushCount > 0 )
    {
        for ( int i = 0; i < n.brushCount; ++i )
            if ( n.data.brushes[i] < cm.brushCount ) brushes[n.data.brushes[i]] = 1;
        return;
    }
    if ( n.brushCount < 0 ) MarkWorldBrushes(index + 1, seen, brushes);
    for ( int i = 0; i < 2; ++i )
        MarkWorldBrushes(index + n.data.children.child[i], seen, brushes);
}

void MarkWorldTriangles(int index, std::vector<unsigned char> &seen, std::vector<unsigned char> &triangles)
{
    const auto& cm=*map;
    if ( index < 0 || index >= cm.aabbCount || seen[index] ) return;
    seen[index] = 1;
    const native::Aabb &n = cm.aabbs[index];
    if ( n.childCount )
    {
        for ( int i = 0; i < n.childCount; ++i ) MarkWorldTriangles(n.firstChildOrPartition + i, seen, triangles);
    }
    else if ( n.firstChildOrPartition >= 0 && n.firstChildOrPartition < cm.partitionCount )
    {
        const native::Partition &p = cm.partitions[n.firstChildOrPartition];
        for ( int i = p.firstTriangle; i < p.firstTriangle + p.triangleCount && i < cm.triangleCount; ++i )
            if ( i >= 0 ) triangles[i] = 1;
    }
}

static bool uploadMap(native::ClipMap& cm)
{
    std::vector<unsigned char> brushMask(cm.brushCount), brushSeen(cm.brushNodeCount);
    std::vector<unsigned char> triMask(cm.triangleCount), treeSeen(cm.aabbCount);
    // World traces start at BSP node zero. The global leaf array can also
    // contain leaves for inline models, whose geometry is in local space.
    std::vector<unsigned char> reached(cm.nodeCount), worldLeaf(cm.leafCount);
    std::vector<int> pending;if(cm.nodeCount)pending.push_back(0);
    while(!pending.empty()){
        int node=pending.back();pending.pop_back();
        if(node<0){int leaf=-1-node;if((unsigned)leaf<cm.leafCount)worldLeaf[leaf]=1;continue;}
        if((unsigned)node>=cm.nodeCount||reached[node])continue;reached[node]=1;
        pending.push_back(cm.nodes[node].children[0]);pending.push_back(cm.nodes[node].children[1]);
    }
    for ( unsigned int i = 0; i < cm.leafCount; ++i )
    {
        if(!worldLeaf[i])continue;
        const native::Leaf &leaf = cm.leafs[i];
        MarkWorldBrushes(leaf.brushNode, brushSeen, brushMask);
        for ( int t = leaf.firstAabb; t < leaf.firstAabb + leaf.aabbCount && t < cm.aabbCount; ++t )
            if ( cm.aabbs[t].material < cm.materialCount && (cm.materials[cm.aabbs[t].material].contents & 0x2820011) )
                MarkWorldTriangles(t, treeSeen, triMask);
    }
    int brushes = 0, failed = 0;
    for ( unsigned int i = 0; i < cm.brushCount; ++i )
    {
        const native::Brush &b = cm.brushes[i];
        if ( !brushMask[i] || !(b.contents & 0x2820011) || b.vertexCount < 4 || !b.vertices )
            continue;
        if ( g_api.add_static_convex(g_world, &b.vertices[0][0], b.vertexCount) == 0 )
            ++brushes;
        else {
            std::vector<BrushPlane> planes;
            for(unsigned axis=0;axis<3;++axis){Vec3 n=v3(0,0,0);(&n.x)[axis]=-1;planes.push_back({n,-b.mins[axis]});(&n.x)[axis]=1;planes.push_back({n,b.maxs[axis]});}
            for(unsigned side=0;side<b.sidesCount;++side){const auto* p=b.sides[side].plane;planes.push_back({v3(p->normal[0],p->normal[1],p->normal[2]),p->dist});}
            std::vector<unsigned> faces;
            bool recovered=BrushFaceTriangles(&b.vertices[0][0],b.vertexCount,planes.data(),unsigned(planes.size()),faces);
            if(!recovered)recovered=BrushPlanarTriangles(&b.vertices[0][0],b.vertexCount,faces);
            if(recovered
                &&g_api.add_static_mesh(g_world,&b.vertices[0][0],b.vertexCount,faces.data(),unsigned(faces.size()/3))==0){
                ++brushes;log("euphoria rd: brush %u recovered from original boundary (%u vertices, %u triangles)\n",i,b.vertexCount,unsigned(faces.size()/3));
            }else{++failed;log("euphoria rd: brush %u could not be imported (%u vertices)\n",i,b.vertexCount);
                if(b.vertexCount<=8)for(unsigned p=0;p<b.vertexCount;++p)log("brush point %.9g %.9g %.9g\n",b.vertices[p][0],b.vertices[p][1],b.vertices[p][2]);}
        }
    }
    int tris = 0;
    bool meshFailed = false;
    if ( cm.triangleCount > 0 && cm.vertices && cm.triangleIndices && cm.vertexCount > 0 )
    {
        std::vector<unsigned int> idx;
        for ( int i = 0; i < cm.triangleCount; ++i )
            if ( triMask[i] )
            {
                // BO1 uses cross(v0-v2,v0-v1) for the outward triangle
                // normal (CM_TracePointThroughTriangle). RAGE uses the
                // opposite winding. Unchanged indices turn floors into
                // downward contact planes and expel a landing body below them.
                idx.push_back(cm.triangleIndices[i*3]);
                idx.push_back(cm.triangleIndices[i*3+2]);
                idx.push_back(cm.triangleIndices[i*3+1]);
            }
        if ( !idx.empty() )
        {
            if ( g_api.add_static_mesh(g_world, &cm.vertices[0][0], cm.vertexCount, idx.data(), (unsigned int)idx.size() / 3) == 0 )
                tris = (int)idx.size() / 3;
            else
                meshFailed = true;
        }
    }
    log( "euphoria rd: map %s: %i brushes (%i not convex), %i triangles\n", cm.name ? cm.name : "?", brushes, failed, tris);
    return failed == 0 && !meshFailed;
}

}
