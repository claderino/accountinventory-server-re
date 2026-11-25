-- ===========================================================
-- Title		:	usp_IncreaseWarehouseItemCount
-- Author		:	ÀÌÃ¢¿ë / Chang-yong Lee (sibukano@ncsoft.com)
-- Create date	:	2013-07-15
-- Description	:	
-- ===========================================================
CREATE PROCEDURE dbo.usp_IncreaseWarehouseItemCount
(
	@OwnerAccountID		UNIQUEIDENTIFIER,
	@LabelID			BIGINT,
	@ItemCount			TINYINT
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
		
		DECLARE	@ownerAccountIdStr	NCHAR(36)	= CAST(@OwnerAccountID AS NCHAR(36));
		-- Parameter validity check
		IF	(	@OwnerAccountID IS NULL
				OR	ISNULL(@LabelID, -1) <= 0
				OR	ISNULL(@ItemCount, 0) = 0	)
			RAISERROR(N'invalid parameter, ownerAccountId:%s, labelId:0x%I64X, itemCount:%d', 16, @ERR_FAILURE, @ownerAccountIdStr, @LabelID, @ItemCount);
		
		IF	(@INIT_TRANCOUNT = 0)
			BEGIN TRAN
		
		BEGIN
			MERGE	dbo.WarehouseStatistics
					WITH	(FORCESEEK)
					AS	T
			USING	(
						SELECT	@OwnerAccountID,
								@LabelID,
								@ItemCount
					)	S
					(	OwnerAccountID,
						LabelID,
						ItemCount	)
			ON		T.OwnerAccountID	= S.OwnerAccountID
			WHEN	MATCHED	THEN
					UPDATE
					SET		MaxLabelID			=	CASE
														WHEN	MaxLabelID < S.LabelID	THEN	S.LabelID
														ELSE	MaxLabelID
													END,
							WarehouseGoodsCount	+=	1,
							WarehouseItemCount	+=	S.ItemCount
			WHEN	NOT MATCHED BY TARGET	THEN
					INSERT	(
						OwnerAccountID,
						MaxLabelID,
						WarehouseGoodsCount,
						WarehouseItemCount
					)
					VALUES	(
						S.OwnerAccountID,
						S.LabelID,
						1,
						S.ItemCount
					)
			OPTION	(LOOP JOIN);
			IF	(@@ROWCOUNT != 1)
				RAISERROR(N'failed to increase warehouse item count, ownerAccountId:%s, labelId:0x%I64X, itemCount:%d', 16, @ERR_FAILURE, @ownerAccountIdStr, @LabelID, @ItemCount);
		END
		
		IF	(@INIT_TRANCOUNT = 0)
			COMMIT;
		
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
