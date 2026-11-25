-- ===========================================================
-- Title		:	usp_RetrieveWarehouseItemCount
-- Author		:	ÀÌÃ¢¿ë / Chang-yong Lee (sibukano@ncsoft.com)
-- Create date	:	2013-07-11
-- Description	:	
-- ===========================================================
CREATE PROCEDURE dbo.usp_RetrieveWarehouseItemCount
(
	@OwnerAccountID		UNIQUEIDENTIFIER,
	@BeginLabelID		BIGINT
)
AS
BEGIN
	SET NOCOUNT, XACT_ABORT ON;
	
	DECLARE	@INIT_TRANCOUNT		INT	= @@TRANCOUNT,
			-- Error Code
			@ERR_SUCCESS		INT = 0,
			@ERR_FAILURE		INT = 1;
	
	BEGIN TRY
		-- Transaction validity check
		IF	(@@NESTLEVEL = 1 AND @INIT_TRANCOUNT > 0)
			RAISERROR(N'invalid initial transaction, init trancount:%d', 16, @ERR_FAILURE, @INIT_TRANCOUNT);
		
		-- Parameter validity check
		IF	(	@OwnerAccountID IS NULL
				OR	ISNULL(@BeginLabelID, -1) <= 0	)	BEGIN
			DECLARE	@ownerAccountIdStr	NCHAR(36) = CAST(@OwnerAccountID AS NCHAR(36));
			RAISERROR(N'invalid parameter, ownerAccountId:%s, beginLabelId:0x%I64X', 16, @ERR_FAILURE, @ownerAccountIdStr, @BeginLabelID);
		END
		
		BEGIN
			DECLARE	@endLabelId		BIGINT	= 0,
					@goodsCount		BIGINT	= 0,
					@itemCount		BIGINT	= 0;
			
			SELECT	@endLabelId		= MaxLabelID,
					@goodsCount		= WarehouseGoodsCount,
					@itemCount		= WarehouseItemCount
			FROM	dbo.WarehouseStatistics
			WHERE	OwnerAccountID	= @OwnerAccountID;
			
			SELECT	@goodsCount - COUNT_BIG(LabelID)								GoodsCount,
					@itemCount - ISNULL(SUM(CAST(RemainingItemCount AS BIGINT)), 0)	ItemCount
			FROM	dbo.WarehouseGoods
			WITH	(INDEX(IX_WarehouseGoods_RegistrationState_OwnerAccountID_LabelID))
			WHERE	RegistrationState	= 2		--	Registration Success
			AND		OwnerAccountID		= @OwnerAccountID
			AND		LabelID				> @BeginLabelID
			AND		LabelID				<= @endLabelId;
		END
		
		RETURN	@ERR_SUCCESS;
	END TRY
	BEGIN CATCH
		IF	(XACT_STATE() != 0 AND @INIT_TRANCOUNT = 0)	BEGIN
			ROLLBACK;
			EXEC dbo.usp_LogError;
		END
		
		DECLARE	@ERR_NUMBER		INT				= ERROR_NUMBER(),
				@ERR_SEVERITY	INT				= ERROR_SEVERITY(),
				@ERR_STATE		INT				= ERROR_STATE(),
				@ERR_PROCEDURE	NVARCHAR(126)	= ERROR_PROCEDURE(),
				@ERR_MESSAGE	NVARCHAR(2048)	= ERROR_MESSAGE(),
				@ERR_CODE		INT				= @ERR_FAILURE;
		
		IF	(@ERR_NUMBER >= 50000)	BEGIN
			IF	(@ERR_PROCEDURE = OBJECT_NAME(@@PROCID))
				SET	@ERR_CODE = @ERR_STATE;
		END
		
		IF	(@@NESTLEVEL = 1)
			RAISERROR(N'[%s], %s', @ERR_SEVERITY, @ERR_CODE, @ERR_PROCEDURE, @ERR_MESSAGE);
		ELSE
			RAISERROR(@ERR_MESSAGE, @ERR_SEVERITY, @ERR_CODE);
		
		RETURN	@ERR_CODE;
	END CATCH
END


(1 rows affected)
