#if !defined(RUNTIMEARRAYSC_H)
#define RUNTIMEARRAYSC_H

//all arrays written so that runtime arrays can be accessed with
//Arr[Y][X] and Arr[Z][Y][X] syntax and have only a single malloc. 

//**if someone wanted to they could use this same approach to create a 4d array
//but even 3d is a little wasteful in terms of memory usage.

//For every array the syntax is the following:
//
// Internal-most index is the column
// The next index moving outward is the plane (outer for 2D arrays)
// The outermost index is the volume (only for 3D arrays)
//
//If allocated statically this would be:
//
// Hypothetical2DArray[RowsPerPlane][ColumnsPerRow]
// Hypothetical3DArray[PlanesPerVolume][RowsPerPlane][ColumnsPerRow]
//
//And Accessed with
//
// Hypothetical2DArray[Row][Column]
// Hypothetical3DArray[Plane][Row][Column]
//
//P.S. I know the syntax is awful, when I think of something better I will change it -
//     feel free to give suggestions!
//
//If you are interested in accessing the array quickly and don't want to march through the pointers,
//there is a 1D accessor available that points directly to the data. Make sure for the asymmetrical
//arrays that you are using the correct number of elements!
template<typename CType_t> 
struct RT_StaticArray2D
{
	int ColumnsPerRow;
	int RowsPerPlane;
	int NumElements;
	//Need to make sure the header is 64-byte aligned as I want this to work with both 32 bit as
	//the pointer arithmetic is wonky if the header is aligned to 32 bits. At least that is the
	//prevailing theory at the moment.
	int PAD;
	CType_t ** Accessor2D;
	CType_t *  Accessor1D;
};

template<typename CType_t> 
struct RT_StaticArray3D
{
	int PlanesPerVolume;
	int ColumnsPerRow;
	int RowsPerPlane;
	int NumElements;
	CType_t *** Accessor3D;
	CType_t *  Accessor1D;
};

template<typename CType_t> 
struct RT_StaticArray2D_ASYM
{
	int RowsPerPlane;
	int NumElements;
	//Need to allocate an array of ints and poll by row to enable correct walking of the asymmetrical array
	int * ColumnsInRow;
	CType_t ** Accessor2D;
	CType_t *  Accessor1D;
};

template<typename CType_t> 
struct RT_StaticArray3D_ASYM
{
	int PlanesPerVolume;
	int NumElements;
	//As is the case above with the columns in the 2D asymmetrical array, every plane can have variable 
	//numbers of Rows, which themselves have variable numbers of columns, so the 2d array is required to
	//correctly poll for the columns.
	int * RowsPerPlane;
	int ** ColumnsByRowAndPlane;
	CType_t *** Accessor3D;
	CType_t *  Accessor1D;
};

//convert everything to char * for pointer arithmetic to make calculation and debugging easier
template<typename CType_t>
RT_StaticArray2D<CType_t> *
Initialize2DArray(int Columns, int Rows)
{
	RT_StaticArray2D<CType_t> * NewArray = 0;
	size_t TotalSize = sizeof(int) * 4 +
					   sizeof(CType_t **) +
					   sizeof(CType_t *) * Rows +
					   sizeof(CType_t *) + 
					   sizeof(CType_t) * Columns * Rows;

	NewArray = (RT_StaticArray2D<CType_t> *)malloc(TotalSize);
	memset((void *)NewArray,0,TotalSize);

	NewArray->ColumnsPerRow = Columns;
	NewArray->RowsPerPlane = Rows;
	NewArray->NumElements = Columns * Rows;
	//Everything is packed into the same allocation, need to jump past the header before beginning to fill in the
	//various pointers (this is the same logic for all RT array types)
	NewArray->Accessor2D = (CType_t **)((char *)NewArray + sizeof(int) * 4 + 
														   sizeof(CType_t **) + 
														   sizeof(CType_t *));
	//Jump ahead of all of the pointers to the various rows. This would be done easily with arithmetic and only require
	//the 1D accessor, but then access the array with[][] syntax would be lost and defeat the entire purpose of this
	//to begin with.
	NewArray->Accessor1D = (CType_t *)((char *)NewArray->Accessor2D + sizeof(CType_t *) * Rows);
	for(int i = 0;
		i < Rows;
		i++)
	{
		NewArray->Accessor2D[i] = (CType_t *)((char *)NewArray->Accessor1D + sizeof(CType_t) * Columns * i );
	}
	return NewArray;
}

template<typename CType_t>
RT_StaticArray3D<CType_t> *
Initialize3DArray(int Columns, int Rows, int Planes)
{
	RT_StaticArray3D<CType_t> * NewArray = 0;
	size_t TotalSize = sizeof(int) * 4 + 
					   sizeof(CType_t ***) + 
					   sizeof(CType_t **) * Planes + 
					   sizeof(CType_t *)  * Rows * Planes + 
					   sizeof(CType_t *) + 
					   sizeof(CType_t) * Columns * Rows * Planes;

	NewArray = (RT_StaticArray3D<CType_t> *)malloc(TotalSize);
	memset((void *)NewArray,0,TotalSize);

	NewArray->ColumnsPerRow = Columns;
	NewArray->RowsPerPlane = Rows;
	NewArray->PlanesPerVolume = Planes;
	NewArray->NumElements = Columns * Rows * Planes;
	NewArray->Accessor3D = (CType_t ***)((char *)NewArray + (sizeof(int) * 4 + 
															 sizeof(CType_t ***) + 
															 sizeof(CType_t *)));
	NewArray->Accessor1D = (CType_t *)((char *)NewArray->Accessor3D + (sizeof(CType_t **) * Planes + 
																	   sizeof(CType_t *) * Planes * Rows) );
	for(int j = 0;
		j < Planes;
		j++)
	{
		NewArray->Accessor3D[j] = (CType_t **)( (char *)NewArray->Accessor3D + (sizeof(CType_t**) * Planes + 
																			    sizeof(CType_t *) * j * Rows ));
		for(int i = 0;
			i < Rows;
			i++)
		{
			NewArray->Accessor3D[j][i] = (CType_t *)((char *)NewArray->Accessor1D + (sizeof(CType_t) * ( Rows * j + i ) * Columns) );
		}
	}
	return NewArray;
}

//this is where things get a little messier because we need to loop through the entire structure to figure out where everthing goes :/
template<typename CType_t>
RT_StaticArray2D_ASYM<CType_t> *
Initialize2DArrayAsym(int * ColumnsInRow, int Rows)
{
	RT_StaticArray2D_ASYM<CType_t> * NewArray = 0;

	//Need to count the total number of elements to determine the size of the
	//int array that will contain the column data. This same concept is repeated
	//for the 3D array, just abstracted slightly.
	int TotalElements = 0;
	for(int i = 0;
		i < Rows;
		i++)
	{
		TotalElements += ColumnsInRow[i];
	}

	size_t TotalSize = sizeof(int *) + sizeof(int) * Rows +
					   sizeof(CType_t *) * Rows +
					   sizeof(int) * 2 +
					   sizeof(CType_t **) +
					   sizeof(CType_t *) + 
					   sizeof(CType_t) * TotalElements;

	NewArray = (RT_StaticArray2D_ASYM<CType_t> *)malloc(TotalSize);
	memset((void *)NewArray, 0, TotalSize);

	NewArray->ColumnsInRow = (int *)((char *)NewArray + sizeof(int) * 2 + 
														sizeof(int *) + 
														sizeof(CType_t **) + 
														sizeof(CType_t *)); 
	NewArray->NumElements = TotalElements;
	NewArray->RowsPerPlane = Rows;
	for(int i = 0;
		i < Rows;
		i++)
	{
		NewArray->ColumnsInRow[i] = ColumnsInRow[i];
	}
	NewArray->Accessor2D = (CType_t **)((char *)NewArray->ColumnsInRow + sizeof(int) * Rows);
	NewArray->Accessor1D = (CType_t *)((char *)NewArray->Accessor2D + sizeof(CType_t *) * Rows);

	int CurrentOffset = 0;
	for(int i = 0;
		i < Rows;
		i++)
	{
		NewArray->Accessor2D[i] = (CType_t *)((char *)NewArray->Accessor1D + CurrentOffset);
		CurrentOffset += sizeof(CType_t) * NewArray->ColumnsInRow[i];
	}
	return NewArray;
}

//Oddly enough, to actually be able to allocate the 3d asymmetrical array you pretty much need a 2d asymmetrical
//array of ints to allocate and properly determine the ColumnsByRowAndPlane struct. Something nice to think about
//in the future would be to figure out how I could write some kind of wrapper that would allow me to allocate the
//arrays more easily. Until I do that, the efficacy of the 3D asymmetrical array is highly suspect.

template<typename CType_t>
RT_StaticArray3D_ASYM<CType_t> *
Initialize3DArrayAsym(int ** ColumnsByRowAndPlane, int * RowsInPlane, int Planes)
{
	RT_StaticArray3D_ASYM<CType_t> * NewArray = 0;

	int TotalElements = 0;
	int TotalRows = 0;
	for(int j = 0;
		j < Planes;
		j++)
	{
		TotalRows += RowsInPlane[j];
		for(int i = 0;
			i < RowsInPlane[j];
			i++)
		{
			TotalElements += ColumnsByRowAndPlane[j][i];
		}
	}

	size_t RowInfoSubArraySize = sizeof(int **) +
								 sizeof(int *) * Planes +
								 sizeof(int) * TotalRows;

	size_t PlaneInfoSubArraySize = sizeof(int *)+
								   sizeof(int) * Planes;

	size_t TotalSize = sizeof(int) * 2 +
					   sizeof(CType_t ***) +
					   sizeof(CType_t *) + 
					   RowInfoSubArraySize +
					   PlaneInfoSubArraySize + 
					   sizeof(CType_t **) * Planes +
					   sizeof(CType_t *) * TotalRows +
					   sizeof(CType_t) * TotalElements;

	NewArray = (RT_StaticArray3D_ASYM<CType_t> *)malloc(TotalSize);
	memset((void *) NewArray, 0, TotalSize);

	NewArray->PlanesPerVolume = Planes;
	NewArray->NumElements = TotalElements;
	NewArray->RowsPerPlane = (int *)((char *)&NewArray->Accessor1D + sizeof(CType_t *));
	for(int i = 0;
		i < Planes;
		i++)
	{
		NewArray->RowsPerPlane[i] = RowsInPlane[i];
	}

	NewArray->ColumnsByRowAndPlane = (int **)((char * )NewArray->RowsPerPlane + sizeof(int) * Planes);
	int Offset = 0;
	for(int j = 0;
		j < Planes;
		j++)
	{
		NewArray->ColumnsByRowAndPlane[j] = (int *)((char *)NewArray->ColumnsByRowAndPlane + sizeof(int *) * Planes + 
																							 Offset);
		for(int i = 0;
			i < RowsInPlane[j];
			i++)
		{
			NewArray->ColumnsByRowAndPlane[j][i] = ColumnsByRowAndPlane[j][i];
			Offset += sizeof(int);
		}
	}

	NewArray->Accessor3D = (CType_t ***)((char *)NewArray->ColumnsByRowAndPlane + sizeof(int *) * Planes + 
																			      Offset);
	NewArray->Accessor1D = (CType_t *)((char *)NewArray->Accessor3D + sizeof(CType_t **) * Planes +
																	  sizeof(CType_t *) * TotalRows);

	int RowOffset = 0;
	int ColumnOffset = 0;
	for(int k = 0;
		k < Planes;
		k++)
	{
		NewArray->Accessor3D[k] = (CType_t **)((char * )NewArray->Accessor3D + sizeof(CType_t **) * Planes + 
																			   sizeof(CType_t *) * RowOffset);
		RowOffset += NewArray->RowsPerPlane[k];
		for(int j = 0;
			j < NewArray->RowsPerPlane[k];
			j++)
		{
			NewArray->Accessor3D[k][j] = (CType_t *)((char *)NewArray->Accessor3D + sizeof(CType_t **) * Planes +
																					sizeof(CType_t *) * TotalRows +
																					sizeof(CType_t) * ColumnOffset);
			ColumnOffset += ColumnsByRowAndPlane[k][j];
		}
	}
	return NewArray;
}

//It was (partially) all for this. There is probably a healthy middle ground between the allocation and freeing
//that would be useful to explore.
template<typename CType_t>
static void
FreeCTArray(RT_StaticArray2D<CType_t> * Array)
{
	free((void *)Array);
}

template<typename CType_t>
static void
FreeCTArray(RT_StaticArray3D<CType_t> * Array)
{
	free((void *)Array);
}

template<typename CType_t>
static void
FreeCTArray(RT_StaticArray2D_ASYM<CType_t> * Array)
{
	free((void *)Array);
}

template<typename CType_t>
static void
FreeCTArray(RT_StaticArray3D_ASYM<CType_t> * Array)
{
	free((void *)Array);
}


#endif
