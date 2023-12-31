// Copyright Epic Games, Inc. All Rights Reserved.

#include "TessellationTable.h"

#ifdef _MSC_VER
#pragma warning(disable : 6385)
#endif

namespace Nanite
{

FTessellationTable::FTessellationTable( uint32 InMaxTessFactor )
	: MaxTessFactor( InMaxTessFactor )
{
	/*
		NumPatterns = (MaxTessFactor + 2) choose 3
		NumPatterns = 1/6 * N(N+1)(N+2)
		= 816
	*/

	uint32 MaxNumTris = MaxTessFactor * MaxTessFactor;
	HashTable.Clear( MaxNumTris, MaxNumTris );

	uint32 NumOffsets = MaxTessFactor * MaxTessFactor * MaxTessFactor;
	OffsetTable.AddUninitialized( NumOffsets + 1 );

	uint32 Shift = FMath::FloorLog2( MaxTessFactor );
	uint32 Mask = MaxTessFactor - 1;

	for( uint32 i = 0; i < NumOffsets; i++ )
	{
		FIntVector TessFactors(
			( (i >> Shift * 0) & Mask ) + 1,
			( (i >> Shift * 1) & Mask ) + 1,
			( (i >> Shift * 2) & Mask ) + 1 );

		FirstVert = Verts.Num();
		FirstTri = Indexes.Num();

		OffsetTable[i].X = FirstVert;
		OffsetTable[i].Y = FirstTri;

		// TessFactors in descending order to reduce size of table.
		if( TessFactors[0] >= TessFactors[1] &&
			TessFactors[1] >= TessFactors[2] )
		{
			//RecursiveSplit( TessFactors );
			UniformTessellateAndSnap( TessFactors );
			//ConstrainToCacheWindow();
		}
	}

	// One more on the end so we can do Num = Offset[i+1] - Offset[i].
	OffsetTable[ NumOffsets ].X = Verts.Num();
	OffsetTable[ NumOffsets ].Y = Indexes.Num();

	HashTable.Free();
}

int32 FTessellationTable::GetPattern( FIntVector TessFactors ) const
{
	checkSlow( 0 < TessFactors[0] && TessFactors[0] <= int32(MaxTessFactor) );
	checkSlow( 0 < TessFactors[1] && TessFactors[1] <= int32(MaxTessFactor) );
	checkSlow( 0 < TessFactors[2] && TessFactors[2] <= int32(MaxTessFactor) );

	if( TessFactors[0] < TessFactors[1] ) Swap( TessFactors[0], TessFactors[1] );
	if( TessFactors[0] < TessFactors[2] ) Swap( TessFactors[0], TessFactors[2] );
	if( TessFactors[1] < TessFactors[2] ) Swap( TessFactors[1], TessFactors[2] );

	return
		( TessFactors[0] - 1 ) +
		( TessFactors[1] - 1 ) * MaxTessFactor +
		( TessFactors[2] - 1 ) * MaxTessFactor * MaxTessFactor;
}

FIntVector FTessellationTable::GetBarycentrics( uint32 Vert ) const
{
	FIntVector Barycentrics;
	Barycentrics.X = Vert & 0xffff;
	Barycentrics.Y = Vert >> 16;
	Barycentrics.Z = BarycentricMax - Barycentrics.X - Barycentrics.Y;
	return Barycentrics;
}

// Average barycentric == average cartesian

float FTessellationTable::LengthSquared( const FIntVector& Barycentrics, const FIntVector& TessFactors ) const
{
	// Barycentric displacement vector:
	// 0 = x + y + z

	FVector3f Norm = FVector3f( Barycentrics ) / BarycentricMax;

	// Length of displacement
	// [ Schindler and Chen 2012, "Barycentric Coordinates in Olympiad Geometry" https://web.evanchen.cc/handouts/bary/bary-full.pdf ]
	return	-Norm.X * Norm.Y * FMath::Square( TessFactors[0] )
			-Norm.Y * Norm.Z * FMath::Square( TessFactors[1] )
			-Norm.Z * Norm.X * FMath::Square( TessFactors[2] );
}

// Snap to exact TessFactor at the edges
void FTessellationTable::SnapAtEdges( FIntVector& Barycentrics, const FIntVector& TessFactors ) const
{
	for( uint32 i = 0; i < 3; i++ )
	{
		const uint32 e0 = i;
		const uint32 e1 = (1 << e0) & 3;

		// Am I on this edge?
		if( Barycentrics[ e0 ] + Barycentrics[ e1 ] == BarycentricMax )
		{
			// Snap toward min barycentric means snapping mirrors. Adjacent patches will thus match.
			uint32 MinIndex = Barycentrics[ e0 ] <  Barycentrics[ e1 ] ? e0 : e1;
			uint32 MaxIndex = Barycentrics[ e0 ] >= Barycentrics[ e1 ] ? e0 : e1;

			// Fixed point round
			uint32 Snapped = ( Barycentrics[ MinIndex ] * TessFactors[i] + (BarycentricMax / 2) - 1 ) & ~( BarycentricMax - 1 );

			Barycentrics[ MinIndex ] = Snapped / TessFactors[i];
			Barycentrics[ MaxIndex ] = BarycentricMax - Barycentrics[ MinIndex ];
		}
	}
}

uint32 FTessellationTable::AddVert( uint32 Vert )
{
	uint32 Hash = MurmurFinalize32( Vert );

	// Find if there already exists one
	uint32 Index;
	for( Index = HashTable.First( Hash ); HashTable.IsValid( Index ); Index = HashTable.Next( Index ) )
	{
		if( Verts[ FirstVert + Index ] == Vert )
		{
			break;
		}
	}
	if( !HashTable.IsValid( Index ) )
	{
		Index = Verts.Add( Vert ) - FirstVert;
		HashTable.Add( Hash, Index );
	}

	return Index;
}

void FTessellationTable::SplitEdge( uint32 TriIndex, uint32 EdgeIndex, uint32 LeftFactor, uint32 RightFactor, const FIntVector& TessFactors )
{
	/*
	===========
		v0
		/\
	e2 /  \ e0
	  /____\
	v2  e1  v1
	===========
	*/

	const uint32 e0 = EdgeIndex;
	const uint32 e1 = (1 << e0) & 3;
	const uint32 e2 = (1 << e1) & 3;

	const uint32 Triangle = Indexes[ TriIndex ];
	const uint32 i0 = ( Triangle >> (e0 * 10) ) & 1023;
	const uint32 i1 = ( Triangle >> (e1 * 10) ) & 1023;
	const uint32 i2 = ( Triangle >> (e2 * 10) ) & 1023;

#if 0
	// Sort verts for deterministic split
	uint32 v[2];
	v[0] = FMath::Min( Verts[ FirstVert + i0 ], Verts[ FirstVert + i1 ] );
	v[1] = FMath::Max( Verts[ FirstVert + i0 ], Verts[ FirstVert + i1 ] );

	uint32 OriginallyZero = 0;
	FIntVector Barycentrics[2];
	for( int j = 0; j < 2; j++ )
	{
		Barycentrics[j] = GetBarycentrics( v[j] );

		// Count how many were zero originally.
		OriginallyZero += Barycentrics[j].X == 0 ?  1 : 0;
		OriginallyZero += Barycentrics[j].Y == 0 ?  4 : 0;
		OriginallyZero += Barycentrics[j].Z == 0 ? 16 : 0;
	}

	FIntVector SplitBarycentrics = Barycentrics[0] * LeftFactor + Barycentrics[1] * RightFactor;

	for( uint32 i = 0; i < 3; i++ )
		SplitBarycentrics[i] = FMath::DivideAndRoundNearest( (uint32)SplitBarycentrics[i], LeftFactor + RightFactor );

	for( uint32 i = 0; i < 3; i++ )
	{
		// If both verts were originally zero then force split to be zero as well.
		if( ( OriginallyZero & 3 ) == 2 )
			SplitBarycentrics[i] = 0;

		OriginallyZero >>= 2;
	}
#else
	// Sort verts for deterministic split
	FIntVector SplitBarycentrics =
		GetBarycentrics( FMath::Min( Verts[ FirstVert + i0 ], Verts[ FirstVert + i1 ] ) ) * LeftFactor +
		GetBarycentrics( FMath::Max( Verts[ FirstVert + i0 ], Verts[ FirstVert + i1 ] ) ) * RightFactor;

	bool bOriginallyZero[3] =
	{
		SplitBarycentrics.X == 0,
		SplitBarycentrics.Y == 0,
		SplitBarycentrics.Z == 0,
	};

	for( uint32 i = 0; i < 3; i++ )
		SplitBarycentrics[i] = FMath::DivideAndRoundNearest( (uint32)SplitBarycentrics[i], LeftFactor + RightFactor );
#endif
	
	uint32 Largest = FMath::Max3Index( SplitBarycentrics[0], SplitBarycentrics[1], SplitBarycentrics[2] );
	uint32 Sum = SplitBarycentrics[0] + SplitBarycentrics[1] + SplitBarycentrics[2];
	SplitBarycentrics[ Largest ] += BarycentricMax - Sum;

	SnapAtEdges( SplitBarycentrics, TessFactors );

	check( SplitBarycentrics[0] + SplitBarycentrics[1] + SplitBarycentrics[2] == BarycentricMax );
	check( !bOriginallyZero[0] || SplitBarycentrics[0] == 0 );
	check( !bOriginallyZero[1] || SplitBarycentrics[1] == 0 );
	check( !bOriginallyZero[2] || SplitBarycentrics[2] == 0 );

	uint32 SplitVert = SplitBarycentrics[0] | ( SplitBarycentrics[1] << 16 );
	uint32 SplitIndex = AddVert( SplitVert );

	checkf( SplitIndex != i0 && SplitIndex != i1 && SplitIndex != i2, TEXT("Degenerate triangle generated") );
	
	// Replace v0
	Indexes.Add( SplitIndex | (i1 << 10) | (i2 << 20) );

	// Replace v1
	Indexes[ TriIndex ] = i0 | ( SplitIndex << 10 ) | (i2 << 20);
}

// Longest edge bisection. Uses Diagsplit rules instead of exact bisection.
void FTessellationTable::RecursiveSplit( const FIntVector& TessFactors )
{
	// Start with patch triangle
	Verts.Add( BarycentricMax + 0 );	// Avoids TArray:Add grabbing reference to constexpr and forcing ODR-use.
	Verts.Add( BarycentricMax << 16 );
	Verts.Add( 0 );

	Indexes.Add( 0 | (1 << 10) | (2 << 20) );

	HashTable.Clear();
	HashTable.Add( Verts[0], 0 );
	HashTable.Add( Verts[1], 1 );
	HashTable.Add( Verts[2], 2 );

	for( int32 TriIndex = FirstTri; TriIndex < Indexes.Num(); )
	{
		float EdgeLength2[3];
		for( uint32 i = 0; i < 3; i++ )
		{
			const uint32 e0 = i;
			const uint32 e1 = (1 << e0) & 3;

			const uint32 Triangle = Indexes[ TriIndex ];
			const uint32 i0 = ( Triangle >> (e0 * 10) ) & 1023;
			const uint32 i1 = ( Triangle >> (e1 * 10) ) & 1023;

			FIntVector b0 = GetBarycentrics( Verts[ FirstVert + i0 ] );
			FIntVector b1 = GetBarycentrics( Verts[ FirstVert + i1 ] );

			EdgeLength2[i] = LengthSquared( b0 - b1, TessFactors );
		}

		uint32 EdgeIndex = FMath::Max3Index( EdgeLength2[0], EdgeLength2[1], EdgeLength2[2] );
		check( EdgeLength2[ EdgeIndex ] >= 0.0f );

		uint32 NumEdgeSplits = FMath::RoundToInt( FMath::Sqrt( EdgeLength2[ EdgeIndex ] ) );
		uint32 HalfSplit = NumEdgeSplits >> 1;

		if( NumEdgeSplits <= 1 )
		{
			// Triangle is small enough
			TriIndex++;
			continue;
		}

		SplitEdge( TriIndex, EdgeIndex, HalfSplit, NumEdgeSplits - HalfSplit, TessFactors );
	}
}

void FTessellationTable::UniformTessellateAndSnap( const FIntVector& TessFactors )
{
	/*
	===========
		v0
		/\
	e2 /  \ e0
	  /____\
	v2  e1  v1
	===========
	*/

	HashTable.Clear();

	const uint32 NumTris = TessFactors[0] * TessFactors[0];

	for( uint32 TriIndex = 0; TriIndex < NumTris; TriIndex++ )
	{
		/*
			Starts from top point. Adds rows of verts and corresponding rows of tri strips.

			|\
		row |\|\
			|\|\|\
			column
		*/

		// Find largest tessellation with NumTris <= TriIndex. These are the preceding tris before this row.
		uint32 TriRow = FMath::FloorToInt( FMath::Sqrt( (float)TriIndex ) );
		uint32 TriCol = TriIndex - TriRow * TriRow;
		/*
			Vert order:
			0    0__1
			|\   \  |
			| \   \ |  <= flip triangle
			|__\   \|
			2   1   2
		*/
		uint32 FlipTri = TriCol & 1;
		uint32 VertCol = TriCol >> 1;

		uint32 VertRowCol[3][2] =
		{
			{ TriRow,		VertCol		},
			{ TriRow + 1,	VertCol + 1	},
			{ TriRow + 1,	VertCol		},
		};
		VertRowCol[1][0] -= FlipTri;
		VertRowCol[2][1] += FlipTri;

		uint32 TriVerts[3];
		for( int Corner = 0; Corner < 3; Corner++ )
		{
			/*
				b0
				|\
			t2  | \  t0
				|__\
			   b2   b1
				 t1
			*/
			FIntVector Barycentrics;
			Barycentrics[0] = TessFactors[0] - VertRowCol[ Corner ][0];
			Barycentrics[1] = VertRowCol[ Corner ][1];
			Barycentrics[2] = VertRowCol[ Corner ][0] - VertRowCol[ Corner ][1];
			Barycentrics *= BarycentricMax;

			// Fixed point round
			Barycentrics[0] = ( Barycentrics[0] + (BarycentricMax / 2) - 1 ) & ~( BarycentricMax - 1 );
			Barycentrics[1] = ( Barycentrics[1] + (BarycentricMax / 2) - 1 ) & ~( BarycentricMax - 1 );
			Barycentrics[2] = ( Barycentrics[2] + (BarycentricMax / 2) - 1 ) & ~( BarycentricMax - 1 );
			Barycentrics /= TessFactors[0];

			{
				const uint32 e0 = FMath::Max3Index( Barycentrics[0], Barycentrics[1], Barycentrics[2] );
				const uint32 e1 = (1 << e0) & 3;
				const uint32 e2 = (1 << e1) & 3;

				Barycentrics[ e0 ] = BarycentricMax - Barycentrics[ e1 ] - Barycentrics[ e2 ];
			}

#if 1
			for( uint32 i = 0; i < 3; i++ )
			{
				const uint32 e0 = i;
				const uint32 e1 = (1 << e0) & 3;
				const uint32 e2 = (1 << e1) & 3;

				if( Barycentrics[ e0 ] == 0 ||
					Barycentrics[ e1 ] == 0 ||
					Barycentrics[ e2 ] == 0 )
					continue;

				uint32 Sum = Barycentrics[ e0 ] + Barycentrics[ e1 ];

#if 0
				// Snap toward min barycentric means snapping mirrors.
				uint32 MinIndex = Barycentrics[ e0 ] <  Barycentrics[ e1 ] ? e0 : e1;
				uint32 MaxIndex = Barycentrics[ e0 ] >= Barycentrics[ e1 ] ? e0 : e1;

				// Fixed point round
				uint32 Snapped = ( Barycentrics[ MinIndex ] * TessFactors[i] + (BarycentricMax / 2) - 1 ) & ~( BarycentricMax - 1 );

				Barycentrics[ MinIndex ] = FMath::Min( Sum, Snapped / TessFactors[i] );
				Barycentrics[ MaxIndex ] = Sum - Barycentrics[ MinIndex ];

				if( Barycentrics[ MinIndex ] > Barycentrics[ MaxIndex ] )
				{
					Barycentrics[ e0 ] = Sum / 2;
					Barycentrics[ e1 ] = Sum - Barycentrics[ e0 ];
				}
#else
				// Fixed point round
				uint32 Snapped = ( Barycentrics[ e0 ] * TessFactors[i] + (BarycentricMax / 2) - 1 ) & ~( BarycentricMax - 1 );

				Barycentrics[ e0 ] = FMath::Min( Sum, Snapped / TessFactors[i] );
				Barycentrics[ e1 ] = Sum - Barycentrics[ e0 ];
#endif
			}
#endif

#if 1
			// Snap verts to the edge if they are close.
			if( Barycentrics.X != 0 &&
				Barycentrics.Y != 0 &&
				Barycentrics.Z != 0 )
			{
				// Find closest point on edge
				uint32 b0 = FMath::Min3Index( Barycentrics[0], Barycentrics[1], Barycentrics[2] );
				uint32 b1 = (1 << b0) & 3;
				uint32 b2 = (1 << b1) & 3;

				//if( Barycentrics[ b1 ] < Barycentrics[ b2 ] )
				//	Swap( b1, b2 );

				uint32 Sum = Barycentrics[ b1 ] + Barycentrics[ b2 ];

				FIntVector ClosestEdgePoint;
				ClosestEdgePoint[ b0 ] = 0;
				ClosestEdgePoint[ b1 ] = ( Barycentrics[ b1 ] * BarycentricMax ) / Sum;
				ClosestEdgePoint[ b2 ] = BarycentricMax - ClosestEdgePoint[ b1 ];

				// Want edge point in its final position so we get the correct distance.
				SnapAtEdges( ClosestEdgePoint, TessFactors );

				float DistSqr = LengthSquared( Barycentrics - ClosestEdgePoint, TessFactors );
				if( DistSqr < 0.25f )
				{
					Barycentrics = ClosestEdgePoint;
				}
			}
#endif

			SnapAtEdges( Barycentrics, TessFactors );

			TriVerts[ Corner ] = Barycentrics[0] | ( Barycentrics[1] << 16 );
		}

		// Degenerate
		if( TriVerts[0] == TriVerts[1] ||
			TriVerts[1] == TriVerts[2] ||
			TriVerts[2] == TriVerts[0] )
			continue;

		uint32 VertIndexes[3];
		for( int Corner = 0; Corner < 3; Corner++ )
			VertIndexes[ Corner ] = AddVert( TriVerts[ Corner ] );

		Indexes.Add( VertIndexes[0] | ( VertIndexes[1] << 10 ) | ( VertIndexes[2] << 20 ) );
	}
}


#define CACHE_WINDOW_SIZE	32

// Weights for individual cache entries based on simulated annealing optimization on DemoLevel.
static int16 CacheWeightTable[ CACHE_WINDOW_SIZE ] = {
	 577,	 616,	 641,  512,		 614,  635,  478,  651,
	  65,	 213,	 719,  490,		 213,  726,  863,  745,
	 172,	 939,	 805,  885,		 958, 1208, 1319, 1318,
	1475,	1779,	2342,  159,		2307, 1998, 1211,  932
};

// Constrain index buffer to only use vertex references that are within a fixed sized trailing window from the current highest encountered vertex index.
// Triangles are reordered based on a FIFO-style cache optimization to minimize the number of vertices that need to be duplicated.
void FTessellationTable::ConstrainToCacheWindow()
{
	uint32 NumOldVertices = Verts.Num() - FirstVert;
	uint32 NumOldTriangles = Indexes.Num() - FirstTri;

	check( MaxTessFactor <= 16 );
	constexpr uint32 MaxNumTris = 16 * 16;
	constexpr uint32 MaxTrianglesInDwords = ( MaxNumTris + 31 ) / 32;

	uint32 VertexToTriangleMasks[ MaxNumTris * 3 ][ MaxTrianglesInDwords ] = {};

	// Generate vertex to triangle masks
	for( uint32 i = 0; i < NumOldTriangles; i++ )
	{
		const uint32 i0 = ( Indexes[ FirstTri + i ] >>  0 ) & 1023;
		const uint32 i1 = ( Indexes[ FirstTri + i ] >> 10 ) & 1023;
		const uint32 i2 = ( Indexes[ FirstTri + i ] >> 20 ) & 1023;
		check( i0 != i1 && i1 != i2 && i2 != i0 ); // Degenerate input triangle!
		check( i0 < NumOldVertices && i1 < NumOldVertices && i2 < NumOldVertices );

		VertexToTriangleMasks[ i0 ][ i >> 5 ] |= 1 << ( i & 31 );
		VertexToTriangleMasks[ i1 ][ i >> 5 ] |= 1 << ( i & 31 );
		VertexToTriangleMasks[ i2 ][ i >> 5 ] |= 1 << ( i & 31 );
	}

	uint32 TrianglesEnabled[ MaxTrianglesInDwords ] = {};	// Enabled triangles are in the current material range and have not yet been visited.
	uint32 TrianglesTouched[ MaxTrianglesInDwords ] = {};	// Touched triangles have had at least one of their vertices visited.

	uint32 NumNewVertices = 0;
	uint32 NumNewTriangles = 0;
	uint16 OldToNewVertex[ MaxNumTris * 3 ];

	uint32 NewVerts[ MaxNumTris * 3 ] = {};	// Initialize to make static analysis happy
	uint32 NewIndexes[ MaxNumTris ];
	
	FMemory::Memset( OldToNewVertex, -1, sizeof( OldToNewVertex ) );

	uint32 DwordEnd	= NumOldTriangles / 32;
	uint32 BitEnd	= NumOldTriangles & 31;

	FMemory::Memset( TrianglesEnabled, -1, DwordEnd * sizeof( uint32 ) );

	if( BitEnd != 0 )
		TrianglesEnabled[ DwordEnd ] = ( 1u << BitEnd ) - 1u;

	auto ScoreVertex = [ &OldToNewVertex, &NumNewVertices ] ( uint32 OldVertex )
	{
		uint16 NewIndex = OldToNewVertex[ OldVertex ];

		int32 CacheScore = 0;
		if( NewIndex != 0xFFFF )
		{
			uint32 CachePosition = ( NumNewVertices - 1 ) - NewIndex;
			if( CachePosition < CACHE_WINDOW_SIZE )
				CacheScore = CacheWeightTable[ CachePosition ];
		}

		return CacheScore;
	};

	while( true )
	{
		uint32 NextTriangleIndex = 0xFFFF;
		int32  NextTriangleScore = 0;

		// Pick highest scoring available triangle
		for( uint32 TriangleDwordIndex = 0; TriangleDwordIndex < MaxTrianglesInDwords; TriangleDwordIndex++ )
		{
			uint32 CandidateMask = TrianglesTouched[ TriangleDwordIndex ] & TrianglesEnabled[ TriangleDwordIndex ];
			while( CandidateMask )
			{
				uint32 TriangleDwordOffset = FMath::CountTrailingZeros( CandidateMask );
				CandidateMask &= CandidateMask - 1;

				int32 TriangleIndex = ( TriangleDwordIndex << 5 ) + TriangleDwordOffset;

				int32 TriangleScore = 0;
				TriangleScore += ScoreVertex( ( Indexes[ FirstTri + TriangleIndex ] >>  0 ) & 1023 );
				TriangleScore += ScoreVertex( ( Indexes[ FirstTri + TriangleIndex ] >> 10 ) & 1023 );
				TriangleScore += ScoreVertex( ( Indexes[ FirstTri + TriangleIndex ] >> 20 ) & 1023 );

				if( TriangleScore > NextTriangleScore )
				{
					NextTriangleIndex = TriangleIndex;
					NextTriangleScore = TriangleScore;
				}
			}
		}

		if( NextTriangleIndex == 0xFFFF )
		{
			// If we didn't find a triangle. It might be because it is part of a separate component. Look for an unvisited triangle to restart from.
			for( uint32 TriangleDwordIndex = 0; TriangleDwordIndex < MaxTrianglesInDwords; TriangleDwordIndex++ )
			{
				uint32 EnableMask = TrianglesEnabled[ TriangleDwordIndex ];
				if( EnableMask )
				{
					NextTriangleIndex = ( TriangleDwordIndex << 5 ) + FMath::CountTrailingZeros( EnableMask );
					break;
				}
			}

			if( NextTriangleIndex == 0xFFFF )
				break;
		}

		uint32 OldIndex[3];
		OldIndex[0] = ( Indexes[ FirstTri + NextTriangleIndex ] >>  0 ) & 1023;
		OldIndex[1] = ( Indexes[ FirstTri + NextTriangleIndex ] >> 10 ) & 1023;
		OldIndex[2] = ( Indexes[ FirstTri + NextTriangleIndex ] >> 20 ) & 1023;

		// Mark incident triangles
		for( uint32 i = 0; i < MaxTrianglesInDwords; i++ )
		{
			TrianglesTouched[i] |= VertexToTriangleMasks[ OldIndex[0] ][i];
			TrianglesTouched[i] |= VertexToTriangleMasks[ OldIndex[1] ][i];
			TrianglesTouched[i] |= VertexToTriangleMasks[ OldIndex[2] ][i];
		}

		uint32 NewIndex[3];
		NewIndex[0] = OldToNewVertex[ OldIndex[0] ];
		NewIndex[1] = OldToNewVertex[ OldIndex[1] ];
		NewIndex[2] = OldToNewVertex[ OldIndex[2] ];

		uint32 NumNew = (NewIndex[0] == 0xFFFF) + (NewIndex[1] == 0xFFFF) + (NewIndex[2] == 0xFFFF);

		// Generate new indices such that they are all within a trailing window of CACHE_WINDOW_SIZE of NumNewVertices.
		// This can require multiple iterations as new/duplicate vertices can push other vertices outside the window.			
		uint32 TestNumNewVertices = NumNewVertices;
		TestNumNewVertices += NumNew;

		while(true)
		{
			if (NewIndex[0] != 0xFFFF && TestNumNewVertices - NewIndex[0] >= CACHE_WINDOW_SIZE)
			{
				NewIndex[0] = 0xFFFF;
				TestNumNewVertices++;
				continue;
			}

			if (NewIndex[1] != 0xFFFF && TestNumNewVertices - NewIndex[1] >= CACHE_WINDOW_SIZE)
			{
				NewIndex[1] = 0xFFFF;
				TestNumNewVertices++;
				continue;
			}

			if (NewIndex[2] != 0xFFFF && TestNumNewVertices - NewIndex[2] >= CACHE_WINDOW_SIZE)
			{
				NewIndex[2] = 0xFFFF;
				TestNumNewVertices++;
				continue;
			}
			break;
		}

		for( int k = 0; k < 3; k++ )
		{
			if( NewIndex[k] == 0xFFFF)
				NewIndex[k] = NumNewVertices++;

			OldToNewVertex[ OldIndex[k] ] = (uint16)NewIndex[k];

			NewVerts[ NewIndex[k] ] = Verts[ FirstVert + OldIndex[k] ];
		}

		// Rotate triangle such that 1st index is smallest
		const uint32 i0 = FMath::Min3Index( NewIndex[0], NewIndex[1], NewIndex[2] );
		const uint32 i1 = (1 << i0) & 3;
		const uint32 i2 = (1 << i1) & 3;

		// Output triangle
		NewIndexes[ NumNewTriangles++ ] = NewIndex[ i0 ] | ( NewIndex[ i1 ] << 10 ) | ( NewIndex[ i2 ] << 20 );

		// Disable selected triangle
		TrianglesEnabled[ NextTriangleIndex >> 5 ] &= ~( 1 << ( NextTriangleIndex & 31 ) );
	}


	check( NumNewTriangles == NumOldTriangles );

	if( NumNewVertices > NumOldVertices )
		Verts.AddUninitialized( NumNewVertices - NumOldVertices );

	// Write back new triangle order
	FMemory::Memcpy( &Verts[ FirstVert ],	NewVerts,	NumNewVertices * sizeof( uint32 ) );
	FMemory::Memcpy( &Indexes[ FirstTri ],	NewIndexes,	NumNewTriangles * sizeof( uint32 ) );
}


FTessellationTable& GetTessellationTable( uint32 MaxTessFactor )
{
	static FTessellationTable TessellationTable( MaxTessFactor );
	return TessellationTable;
}

} // namespace Nanite
